// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
#[macro_use]
mod common;
mod crypto_provider;
mod key_manager;
mod kms_asymmetric_key;

use crate::key_manager::KeyManager;

use failure::{Error, ResultExt};
use fidl::endpoints::{RequestStream, ServiceMarker};
use fidl_fuchsia_kms::{KeyManagerMarker, KeyManagerRequestStream};
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use futures::prelude::*;
use log::error;
use std::sync::Arc;

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let key_manager = Arc::new(KeyManager::new());
    let fut = ServicesServer::new()
        .add_service((KeyManagerMarker::NAME, move |chan| spawn(chan, Arc::clone(&key_manager))))
        .start()
        .context("Error starting KMS services server")?;

    executor.run_singlethreaded(fut).context("Error executing KMS future")?;
    Ok(())
}

fn spawn(chan: fasync::Channel, key_manager: Arc<KeyManager>) {
    let mut stream = KeyManagerRequestStream::from_channel(chan);

    fasync::spawn(
        async move {
            while let Some(r) = await!(stream.try_next())? {
                key_manager.handle_request(r)?;
            }
            Ok(())
        }
            .unwrap_or_else(|e: fidl::Error| error!("Error handling KMS request: {:?}", e)),
    );
}
