// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
#![deny(warnings)]

// Explicitly added due to conflict using custom_attribute and async_await above.
#[macro_use]
extern crate serde_derive;

mod opts;

use crate::opts::Opt;
use connectivity_testing::wlan_ap_service_util;
use connectivity_testing::wlan_service_util;
use failure::{bail, Error, ResultExt};
use fidl_fuchsia_wlan_device_service::{DeviceServiceMarker, DeviceServiceProxy};
use fidl_fuchsia_wlan_sme as fidl_sme;
use fuchsia_app::client::connect_to_service;
use fuchsia_async as fasync;
use fuchsia_syslog::{self as syslog, fx_log_err, fx_log_info};
use fuchsia_zircon as zx;
use std::collections::HashMap;
use std::process;
use structopt::StructOpt;

#[allow(dead_code)]
type WlanService = DeviceServiceProxy;

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["wlan-ap-smoke-test"]).expect("should not fail");

    let opt = Opt::from_args();
    fx_log_info!("{:?}", opt);

    // create objects to hold test objects and results
    let mut test_results: TestResults = Default::default();

    let mut test_pass = true;
    if let Err(e) = run_test(opt, &mut test_results) {
        test_pass = false;
        test_results.error_message = e.to_string();
    }

    report_results(&mut test_results);

    if !test_pass {
        process::exit(1);
    }

    Ok(())
}

fn run_test(opt: Opt, test_results: &mut TestResults) -> Result<(), Error> {
    let target_ssid = opt.target_ssid.as_bytes();
    let target_pwd = opt.target_pwd.as_bytes();
    let target_channel = opt.target_channel;
    let mut test_pass = false;
    let mut exec = fasync::Executor::new().context("error creating event loop")?;
    let wlan_svc =
        connect_to_service::<DeviceServiceMarker>().context("Failed to connect to wlan_service")?;
    test_results.connect_to_wlan_service = true;

    let fut = async {
        let wlan_iface_ids = await!(wlan_service_util::get_iface_list(&wlan_svc))
            .context("wlan-ap-smoke-test: failed to query wlanservice iface list")?;
        test_results.query_wlan_service_iface_list = true;

        if wlan_iface_ids.is_empty() {
            bail!("Did not find wlan interfaces");
        };

        test_results.wlan_client_discovered = false;
        test_results.wlan_ap_discovered = false;

        // find MAC role supported by each interface
        for iface in wlan_iface_ids {
            let (status, response) =
                await!(wlan_svc.query_iface(iface)).context("error querying iface")?;

            if status == zx::sys::ZX_OK {
                let query_iface_response = match response {
                    Some(response) => response,
                    _ => panic!("Expected a valid list response"),
                };

                if query_iface_response.role == fidl_fuchsia_wlan_device::MacRole::Client {
                    match await!(wlan_service_util::get_iface_sme_proxy(&wlan_svc, iface)) {
                        Ok(client_sme_proxy) => {
                            match await!(client_sme_proxy.status()) {
                                Ok(_status) => {
                                    let mut wlan_client_ifaces =
                                        WlanClientIface::new(client_sme_proxy);
                                    wlan_client_ifaces.client_interface_status = true;
                                    test_results.wlan_client_discovered = true;
                                    test_results
                                        .iface_client_objects
                                        .insert(iface, wlan_client_ifaces);
                                }
                                Err(_) => {
                                    continue;
                                }
                            };
                        }
                        Err(_) => {
                            continue;
                        }
                    }
                }

                if query_iface_response.role == fidl_fuchsia_wlan_device::MacRole::Ap {
                    match await!(wlan_ap_service_util::get_iface_ap_sme_proxy(&wlan_svc, iface)) {
                        Ok(ap_sme_proxy) => {
                            let mut wlan_ap_iface = WlanApIface::new(ap_sme_proxy);
                            wlan_ap_iface.ap_interface_status = true;
                            test_results.iface_ap_objects.insert(iface, wlan_ap_iface);
                            test_results.wlan_ap_discovered = true;
                            test_pass = true;
                        }
                        Err(_) => {
                            bail!("Expected a valid ap_sme_proxy for interface {:?}", iface);
                        }
                    }
                }
            }
        }

        // Go over all APs, create a network, have every client scan, connect and disconnect.
        // When done, stop the AP.
        for (ap_iface_id, wlan_ap_iface) in test_results.iface_ap_objects.iter_mut() {
            let start_ap_result_code = await!(wlan_ap_service_util::start_ap(
                &wlan_ap_iface.sme_proxy,
                target_ssid.to_vec(),
                target_pwd.to_vec(),
                target_channel
            ));

            match start_ap_result_code {
                Ok(result_code) => match result_code {
                    fidl_sme::StartApResultCode::Success => {
                        fx_log_info!("AP started successfully");
                        wlan_ap_iface.ap_channel = target_channel;
                        wlan_ap_iface.ap_start_success = true;
                    }
                    _ => {
                        fx_log_err!("Error starting AP: {:?}", result_code);
                        test_pass = false;
                        continue;
                    }
                },
                _ => {
                    continue;
                }
            };

            // Scan and look for a specific network
            for (client_iface_id, wlan_client_iface) in test_results.iface_client_objects.iter_mut()
            {
                // Move on if it's the same interface
                if ap_iface_id == client_iface_id {
                    continue;
                }

                let mut wlan_client_results = WlanClientResultsPerAP::new();

                let scan_results_return =
                    await!(wlan_service_util::perform_scan(&wlan_client_iface.sme_proxy));

                let scan_results = match scan_results_return {
                    Ok(scan_results) => scan_results,
                    Err(_) => {
                        test_pass = false;
                        wlan_client_results.found_ap_in_scan = false;
                        continue;
                    }
                };

                if scan_results.iter().find(|&ap| ap.best_bss.ssid == target_ssid.to_vec()) != None
                {
                    wlan_client_results.found_ap_in_scan = true;
                }

                // Connect to network, if found in scan results
                if wlan_client_results.found_ap_in_scan == true {
                    let connect_result = await!(wlan_service_util::connect_to_network(
                        &wlan_client_iface.sme_proxy,
                        target_ssid.to_vec(),
                        target_pwd.to_vec()
                    ));

                    match connect_result {
                        Ok(true) => {
                            wlan_client_results.connection_success = true;
                        }
                        _ => continue,
                    };
                }

                // If connected, disconnect
                if wlan_client_results.connection_success == true {
                    match await!(wlan_service_util::disconnect_from_network(
                        &wlan_client_iface.sme_proxy
                    )) {
                        Err(_) => wlan_client_results.disconnect_success = false,
                        _ => wlan_client_results.disconnect_success = true,
                    };
                }

                if wlan_ap_iface.ap_start_success
                    && wlan_client_results.found_ap_in_scan
                    && wlan_client_results.connection_success
                    && wlan_client_results.disconnect_success
                    && wlan_client_results.dhcp_success
                    && wlan_client_results.data_transfer
                    && test_pass == true
                {
                    test_pass = true;
                } else {
                    test_pass = false;
                }

                wlan_ap_iface.iface_client_results.insert(*client_iface_id, wlan_client_results);
            }

            if wlan_ap_iface.ap_start_success == true {
                // Stop AP
                let stop_ap_result =
                    await!(wlan_ap_service_util::stop_ap(&wlan_ap_iface.sme_proxy));

                if stop_ap_result.is_ok() {
                    wlan_ap_iface.ap_stop_success = true;
                } else {
                    println!("Couldn't stop AP on interface {:?}", ap_iface_id);
                }

                if test_pass == true && wlan_ap_iface.ap_stop_success == true {
                    test_pass = true;
                }
            }
        }

        Ok(())
    };
    exec.run_singlethreaded(fut)?;

    if !test_pass {
        bail!("Saw a failure on at least one interface");
    }

    Ok(())
}

// Object to hold overall test status
#[derive(Default, Serialize)]
struct TestResults {
    connect_to_wlan_service: bool,
    query_wlan_service_iface_list: bool,
    wlan_ap_discovered: bool,
    wlan_client_discovered: bool,

    #[serde(flatten)]
    iface_ap_objects: HashMap<u16, WlanApIface>,

    #[serde(flatten)]
    iface_client_objects: HashMap<u16, WlanClientIface>,

    error_message: String,
}

// Object to hold test specific status
#[derive(Serialize)]
struct WlanClientIface {
    #[serde(skip_serializing)]
    sme_proxy: fidl_sme::ClientSmeProxy,

    client_interface_status: bool,
}

impl WlanClientIface {
    pub fn new(sme_proxy: fidl_sme::ClientSmeProxy) -> WlanClientIface {
        WlanClientIface { sme_proxy: sme_proxy, client_interface_status: false }
    }
}

// Object to hold test specific status
#[derive(Serialize)]
struct WlanClientResultsPerAP {
    found_ap_in_scan: bool,

    connection_success: bool,

    disconnect_success: bool,

    dhcp_success: bool,

    data_transfer: bool,
}

impl WlanClientResultsPerAP {
    pub fn new() -> WlanClientResultsPerAP {
        WlanClientResultsPerAP {
            found_ap_in_scan: false,
            connection_success: false,
            disconnect_success: false,
            dhcp_success: false,
            data_transfer: false,
        }
    }
}

#[derive(Serialize)]
struct WlanApIface {
    #[serde(skip_serializing)]
    sme_proxy: fidl_sme::ApSmeProxy,

    ap_interface_status: bool,
    ap_start_success: bool,
    ap_channel: u8,
    ap_stop_success: bool,

    #[serde(flatten)]
    iface_client_results: HashMap<u16, WlanClientResultsPerAP>,
}

impl WlanApIface {
    pub fn new(sme_proxy: fidl_sme::ApSmeProxy) -> WlanApIface {
        WlanApIface {
            sme_proxy: sme_proxy,
            ap_interface_status: false,
            ap_start_success: false,
            ap_stop_success: false,
            ap_channel: 0,
            iface_client_results: HashMap::new(),
        }
    }
}

fn report_results(test_results: &TestResults) {
    println!("{}", serde_json::to_string_pretty(&test_results).unwrap());
}
