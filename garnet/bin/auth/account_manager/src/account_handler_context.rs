// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_auth::{AuthProviderConfig, AuthProviderMarker};
use fidl_fuchsia_auth_account::Status;
use fidl_fuchsia_auth_account_internal::{
    AccountHandlerContextRequest, AccountHandlerContextRequestStream,
};
use futures::prelude::*;
use std::collections::HashMap;
use token_manager::AuthProviderConnection;

/// A type that can respond to`AccountHandlerContext` requests from the AccountHandler components
/// that we launch. These requests provide contextual and service information to the
/// AccountHandlers, such as connections to components implementing the `AuthProviderFactory`
/// interface.
pub struct AccountHandlerContext {
    /// A map from auth_provider_type to an `AuthProviderConnection` used to launch the associated
    /// component.
    auth_provider_connections: HashMap<String, AuthProviderConnection>,
    account_dir_parent: String,
}

impl AccountHandlerContext {
    /// Creates a new `AccountHandlerContext` from the supplied vector of `AuthProviderConfig`
    /// objects.
    pub fn new(
        auth_provider_configs: &[AuthProviderConfig],
        account_dir_parent: &str,
    ) -> AccountHandlerContext {
        AccountHandlerContext {
            auth_provider_connections: auth_provider_configs
                .iter()
                .map(|apc| {
                    (apc.auth_provider_type.clone(), AuthProviderConnection::from_config_ref(apc))
                })
                .collect(),
            account_dir_parent: account_dir_parent.to_string(),
        }
    }

    /// Asynchronously handles the supplied stream of `AccountHandlerContextRequest` messages.
    pub async fn handle_requests_from_stream(
        &self,
        mut stream: AccountHandlerContextRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = await!(stream.try_next())? {
            await!(self.handle_request(req))?;
        }
        Ok(())
    }

    /// Asynchronously handles a single `AccountHandlerContextRequest`.
    async fn handle_request(&self, req: AccountHandlerContextRequest) -> Result<(), fidl::Error> {
        match req {
            AccountHandlerContextRequest::GetAuthProvider {
                auth_provider_type,
                auth_provider,
                responder,
            } => responder.send(await!(self.get_auth_provider(&auth_provider_type, auth_provider))),
            AccountHandlerContextRequest::GetAccountDirParent { responder } => {
                responder.send(self.get_account_dir_parent())
            }
        }
    }

    fn get_account_dir_parent(&self) -> &str {
        &self.account_dir_parent
    }

    async fn get_auth_provider<'a>(
        &'a self,
        auth_provider_type: &'a str,
        auth_provider: ServerEnd<AuthProviderMarker>,
    ) -> Status {
        match self.auth_provider_connections.get(auth_provider_type) {
            Some(apc) => match await!(apc.connect(auth_provider)) {
                Ok(()) => Status::Ok,
                Err(_) => Status::UnknownError,
            },
            None => Status::NotFound,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Note: Most AccountHandlerContext methods launch instances of an AuthProvider. Since its
    /// currently not convenient to mock out this component launching in Rust, we rely on the
    /// hermetic component test to provide coverage for these areas and only cover the in-process
    /// behavior with this unit-test.

    const TEST_ACCOUNT_DIR_PARENT: &str = "/data/test_account";

    #[test]
    fn test_new() {
        let dummy_configs = vec![
            AuthProviderConfig {
                auth_provider_type: "dummy_1".to_string(),
                url: "fuchsia-pkg://fuchsia.com/dummy_ap_1#meta/ap.cmx".to_string(),
                params: Some(vec!["test_arg_1".to_string()]),
            },
            AuthProviderConfig {
                auth_provider_type: "dummy_2".to_string(),
                url: "fuchsia-pkg://fuchsia.com/dummy_ap_2#meta/ap.cmx".to_string(),
                params: None,
            },
        ];
        let dummy_config_1 = &dummy_configs[0];
        let dummy_config_2 = &dummy_configs[1];

        let test_object = AccountHandlerContext::new(&dummy_configs, TEST_ACCOUNT_DIR_PARENT);
        let test_connection_1 =
            test_object.auth_provider_connections.get(&dummy_config_1.auth_provider_type).unwrap();
        let test_connection_2 =
            test_object.auth_provider_connections.get(&dummy_config_2.auth_provider_type).unwrap();

        assert_eq!(test_object.get_account_dir_parent(), TEST_ACCOUNT_DIR_PARENT);
        assert_eq!(test_connection_1.component_url(), dummy_config_1.url);
        assert_eq!(test_connection_2.component_url(), dummy_config_2.url);
        assert!(test_object.auth_provider_connections.get("bad url").is_none());
    }
}
