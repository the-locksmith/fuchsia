// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(async_await, await_macro, futures_api)]

use std::borrow::Cow;
use std::collections::HashMap;
use std::fs;
use std::os::unix::io::AsRawFd;
use std::path;
use std::sync::Arc;

use failure::{self, ResultExt};
use fidl::endpoints::{RequestStream, ServiceMarker};
use fidl_fuchsia_net_dns::DnsPolicyMarker;
use fidl_fuchsia_net_policy::{ObserverMarker, ObserverRequestStream};
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_err, fx_log_info};
use futures::lock::Mutex;
use futures::{self, StreamExt, TryFutureExt, TryStreamExt};
use serde_derive::Deserialize;

mod device_id;
mod dns_policy_service;
mod interface;
mod matchers;
mod observer_service;

#[derive(Debug, Deserialize)]
pub struct DnsConfig {
    pub servers: Vec<std::net::IpAddr>,
}

#[derive(Debug, Deserialize)]
struct Config {
    pub device_name: Option<String>,
    pub dns_config: DnsConfig,
    #[serde(deserialize_with = "matchers::InterfaceSpec::parse_as_tuples")]
    pub rules: Vec<matchers::InterfaceSpec>,
}

impl Config {
    pub fn load<P: AsRef<path::Path>>(path: P) -> Result<Self, failure::Error> {
        let path = path.as_ref();
        let file = fs::File::open(path)
            .with_context(|_| format!("could not open the config file {}", path.display()))?;
        let config = serde_json::from_reader(file).with_context(|_| {
            format!("could not deserialize the config file {}", path.display())
        })?;
        Ok(config)
    }
}

fn derive_device_name(interfaces: Vec<fidl_fuchsia_netstack::NetInterface>) -> Option<String> {
    interfaces
        .iter()
        .filter(|iface| {
            fidl_fuchsia_hardware_ethernet_ext::EthernetFeatures::from_bits_truncate(iface.features)
                .is_physical()
        })
        .min_by(|a, b| a.id.cmp(&b.id))
        .map(|iface| device_id::device_id(&iface.hwaddr))
}

static DEVICE_NAME_KEY: &str = "DeviceName";

fn main() -> Result<(), failure::Error> {
    fuchsia_syslog::init_with_tags(&["netcfg"])?;
    fx_log_info!("Started");
    let Config { device_name, dns_config: DnsConfig { servers }, rules: default_config_rules } =
        Config::load("/pkg/data/default.json")?;
    let mut persisted_interface_config =
        interface::FileBackedConfig::load(&"/data/net_interfaces.cfg.json")?;
    let mut executor = fuchsia_async::Executor::new().context("could not create executor")?;
    let stack = fuchsia_app::client::connect_to_service::<fidl_fuchsia_net_stack::StackMarker>()
        .context("could not connect to stack")?;
    let netstack =
        fuchsia_app::client::connect_to_service::<fidl_fuchsia_netstack::NetstackMarker>()
            .context("could not connect to netstack")?;
    let device_settings_manager = fuchsia_app::client::connect_to_service::<
        fidl_fuchsia_devicesettings::DeviceSettingsManagerMarker,
    >()
    .context("could not connect to device settings manager")?;

    let mut servers = servers
        .into_iter()
        .map(fidl_fuchsia_net_ext::IpAddress)
        .map(Into::into)
        .collect::<Vec<fidl_fuchsia_net::IpAddress>>();

    let () =
        netstack.set_name_servers(&mut servers.iter_mut()).context("could not set name servers")?;

    let default_device_name = device_name.as_ref().map(Cow::Borrowed).map(Ok);

    let mut device_name_stream = futures::stream::iter(default_device_name).chain(
        netstack.take_event_stream().try_filter_map(
            |fidl_fuchsia_netstack::NetstackEvent::OnInterfacesChanged { interfaces }| {
                futures::future::ok(derive_device_name(interfaces).map(Cow::Owned))
            },
        ),
    );

    let device_name = async {
        match await!(device_name_stream.try_next())
            .context("netstack event stream ended before providing interfaces")?
        {
            Some(device_name) => {
                let success =
                    await!(device_settings_manager.set_string(DEVICE_NAME_KEY, &device_name))?;
                Ok(success)
            }
            None => {
                Err(failure::err_msg("netstack event stream ended before providing interfaces"))
            }
        }
    };

    const ETHDIR: &str = "/dev/class/ethernet";
    let mut interface_ids = HashMap::new();
    // TODO(chunyingw): Add the loopback interfaces through netcfg
    // Remove the following hardcoded nic id 1.
    interface_ids.insert("lo".to_owned(), 1);
    let interface_ids = Arc::new(Mutex::new(interface_ids));

    let ethdir = fs::File::open(ETHDIR).with_context(|_| format!("could not open {}", ETHDIR))?;
    let mut watcher = fuchsia_vfs_watcher::Watcher::new(&ethdir)
        .with_context(|_| format!("could not watch {}", ETHDIR))?;
    let ethernet_device = async {
        while let Some(fuchsia_vfs_watcher::WatchMessage { event, filename }) =
            await!(watcher.try_next()).with_context(|_| format!("watching {}", ETHDIR))?
        {
            match event {
                fuchsia_vfs_watcher::WatchEvent::ADD_FILE
                | fuchsia_vfs_watcher::WatchEvent::EXISTING => {
                    let filename = path::Path::new(ETHDIR).join(filename);
                    let device = fs::File::open(&filename)
                        .with_context(|_| format!("could not open {}", filename.display()))?;
                    let topological_path =
                        fdio::device_get_topo_path(&device).with_context(|_| {
                            format!("fdio::device_get_topo_path({})", filename.display())
                        })?;
                    let device = device.as_raw_fd();
                    let mut client = 0;
                    // Safe because we're passing a valid fd.
                    let () = fuchsia_zircon::Status::ok(unsafe {
                        fdio::fdio_sys::fdio_get_service_handle(device, &mut client)
                    })
                    .with_context(|_| {
                        format!(
                            "fuchsia_zircon::sys::fdio_get_service_handle({})",
                            filename.display()
                        )
                    })?;
                    // Safe because we checked the return status above.
                    let client = fuchsia_zircon::Channel::from(unsafe {
                        fuchsia_zircon::Handle::from_raw(client)
                    });

                    let device = fidl::endpoints::ClientEnd::<
                        fidl_fuchsia_hardware_ethernet::DeviceMarker,
                    >::new(client)
                    .into_proxy()?;
                    let device_info = await!(device.get_info())?;
                    let device_info: fidl_fuchsia_hardware_ethernet_ext::EthernetInfo =
                        device_info.into();

                    if device_info.features.is_physical() {
                        let client = device
                            .into_channel()
                            .map_err(|fidl_fuchsia_hardware_ethernet::DeviceProxy { .. }| {
                                failure::err_msg("failed to convert device proxy into channel")
                            })?
                            .into_zx_channel();

                        let name = persisted_interface_config.get_stable_name(
                            topological_path.clone(), /* TODO(tamird): we can probably do
                                                       * better with std::borrow::Cow. */
                            device_info.mac,
                            device_info.features.contains(
                                fidl_fuchsia_hardware_ethernet_ext::EthernetFeatures::WLAN,
                            ),
                        )?;

                        let mut derived_interface_config = matchers::config_for_device(
                            &device_info,
                            name.to_string(),
                            &topological_path,
                            &default_config_rules,
                        );
                        let nic_id = await!(netstack.add_ethernet_device(
                            &topological_path,
                            &mut derived_interface_config,
                            fidl::endpoints::ClientEnd::<
                                fidl_fuchsia_hardware_ethernet::DeviceMarker,
                            >::new(client),
                        ))
                        .with_context(|_| {
                            format!(
                                "fidl_netstack::Netstack::add_ethernet_device({})",
                                filename.display()
                            )
                        })?;

                        await!(match derived_interface_config.ip_address_config {
                            fidl_fuchsia_netstack::IpAddressConfig::Dhcp(_) => {
                                netstack.set_dhcp_client_status(nic_id as u32, true)
                            }
                            fidl_fuchsia_netstack::IpAddressConfig::StaticIp(
                                fidl_fuchsia_net::Subnet { addr: mut address, prefix_len },
                            ) => netstack.set_interface_address(
                                nic_id as u32,
                                &mut address,
                                prefix_len
                            ),
                        })?;
                        let () = netstack.set_interface_status(nic_id as u32, true)?;

                        // TODO(chunyingw): when netcfg switches to stack.add_ethernet_interface,
                        // remove casting nic_id to u64.
                        await!(interface_ids.lock())
                            .insert(derived_interface_config.name, nic_id as u64);
                    }
                }

                fuchsia_vfs_watcher::WatchEvent::IDLE
                | fuchsia_vfs_watcher::WatchEvent::REMOVE_FILE => {}
                event => {
                    let () = Err(failure::format_err!("unknown WatchEvent {:?}", event))?;
                }
            }
        }
        Ok(())
    };

    let interface_ids = interface_ids.clone();
    let netstack_service = netstack.clone();

    let fidl_service_fut = ServicesServer::new()
        .add_service((DnsPolicyMarker::NAME, move |channel| {
            dns_policy_service::spawn_net_dns_fidl_server(netstack_service.clone(), channel);
        }))
        .add_service((ObserverMarker::NAME, move |channel| {
            fasync::spawn(
                observer_service::serve_fidl_requests(
                    stack.clone(),
                    ObserverRequestStream::from_channel(channel),
                    interface_ids.clone(),
                )
                .unwrap_or_else(|e| fx_log_err!("failed to serve_fidl_requests:{}", e)),
            );
        }))
        .start()
        .context("error starting netcfg services server")?;

    let (_success, (), ()) =
        executor.run_singlethreaded(device_name.try_join3(ethernet_device, fidl_service_fut))?;
    Ok(())
}
