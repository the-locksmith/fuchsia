// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::common::ASYMMETRIC_KEY_ALGORITHMS;
use crate::crypto_provider::{
    AsymmetricProviderKey, CryptoProvider, CryptoProviderError, ProviderKey, SealingProviderKey,
};
use fidl_fuchsia_kms::AsymmetricKeyAlgorithm;
use std::sync::{Arc, Mutex};

/// A mock crypto provider implementation.
///
/// This mock implementation is capable of recording the input data and be provided with output
/// data. Note that fields are references, so tester is able to keep a clone of the MockProvider
/// and use that to change the result during the test.
#[derive(Debug)]
pub struct MockProvider {
    // Use Mutex to achieve interior mutability since this provider is expected to be not
    // mutable.
    key_name: Arc<Mutex<Option<String>>>,
    key_data: Arc<Mutex<Option<Vec<u8>>>>,
    result: Arc<Mutex<Result<Vec<u8>, CryptoProviderError>>>,
    key_result: Arc<Mutex<Result<Vec<u8>, CryptoProviderError>>>,
}

impl CryptoProvider for MockProvider {
    fn supported_asymmetric_algorithms(&self) -> Vec<AsymmetricKeyAlgorithm> {
        // The mock provider supported all asymmetric algorithms.
        ASYMMETRIC_KEY_ALGORITHMS.to_vec()
    }
    fn get_name(&self) -> &'static str {
        "MockProvider"
    }
    fn box_clone(&self) -> Box<dyn CryptoProvider> {
        Box::new(MockProvider {
            key_name: Arc::clone(&self.key_name),
            key_data: Arc::clone(&self.key_data),
            result: Arc::clone(&self.result),
            key_result: Arc::clone(&self.key_result),
        })
    }
    fn generate_asymmetric_key(
        &self,
        key_algorithm: AsymmetricKeyAlgorithm,
        key_name: &str,
    ) -> Result<Box<AsymmetricProviderKey>, CryptoProviderError> {
        *self.key_name.lock().unwrap() = Some(key_name.to_string());
        let result: &Result<Vec<u8>, CryptoProviderError> = &self.result.lock().unwrap();
        match result {
            Err(err) => Err(err.clone()),
            Ok(key_data) => Ok(Box::new(MockAsymmetricKey {
                key_data: key_data.to_vec(),
                key_algorithm,
                result: Arc::clone(&self.key_result),
            })),
        }
    }
    fn import_asymmetric_key(
        &self,
        key_data: &[u8],
        key_algorithm: AsymmetricKeyAlgorithm,
        key_name: &str,
    ) -> Result<Box<AsymmetricProviderKey>, CryptoProviderError> {
        *self.key_name.lock().unwrap() = Some(key_name.to_string());
        *self.key_data.lock().unwrap() = Some(key_data.to_vec());
        let result: &Result<Vec<u8>, CryptoProviderError> = &self.result.lock().unwrap();
        match result {
            Err(err) => Err(err.clone()),
            Ok(result_data) => Ok(Box::new(MockAsymmetricKey {
                key_data: result_data.clone(),
                key_algorithm,
                result: Arc::clone(&self.key_result),
            })),
        }
    }

    fn parse_asymmetric_key(
        &self,
        key_data: &[u8],
        key_algorithm: AsymmetricKeyAlgorithm,
    ) -> Result<Box<AsymmetricProviderKey>, CryptoProviderError> {
        *self.key_data.lock().unwrap() = Some(key_data.to_vec());
        let result: &Result<Vec<u8>, CryptoProviderError> = &self.result.lock().unwrap();
        match result {
            Err(err) => Err(err.clone()),
            Ok(_) => Ok(Box::new(MockAsymmetricKey {
                key_data: key_data.to_vec(),
                key_algorithm,
                result: Arc::clone(&self.key_result),
            })),
        }
    }

    fn generate_sealing_key(
        &self,
        key_name: &str,
    ) -> Result<Box<SealingProviderKey>, CryptoProviderError> {
        *self.key_name.lock().unwrap() = Some(key_name.to_string());
        let result: &Result<Vec<u8>, CryptoProviderError> = &self.result.lock().unwrap();
        match result {
            Err(err) => Err(err.clone()),
            Ok(key_data) => Ok(Box::new(MockSealingKey {
                key_data: key_data.to_vec(),
                result: Arc::clone(&self.key_result),
            })),
        }
    }

    fn parse_sealing_key(
        &self,
        key_data: &[u8],
    ) -> Result<Box<SealingProviderKey>, CryptoProviderError> {
        *self.key_data.lock().unwrap() = Some(key_data.to_vec());
        let result: &Result<Vec<u8>, CryptoProviderError> = &self.result.lock().unwrap();
        match result {
            Err(err) => Err(err.clone()),
            Ok(_) => Ok(Box::new(MockSealingKey {
                key_data: key_data.to_vec(),
                result: Arc::clone(&self.key_result),
            })),
        }
    }
}

#[allow(dead_code)]
impl MockProvider {
    pub fn new() -> Self {
        MockProvider {
            key_name: Arc::new(Mutex::new(None)),
            key_data: Arc::new(Mutex::new(None)),
            result: Arc::new(Mutex::new(Err(CryptoProviderError::new("")))),
            key_result: Arc::new(Mutex::new(Err(CryptoProviderError::new("")))),
        }
    }
    pub fn set_result(&self, result: &[u8]) {
        *self.result.lock().unwrap() = Ok(result.to_vec());
    }

    pub fn set_error(&self) {
        *self.result.lock().unwrap() = Err(CryptoProviderError::new(""));
    }

    pub fn set_key_result(&self, key_result: Result<Vec<u8>, CryptoProviderError>) {
        *self.key_result.lock().unwrap() = key_result;
    }

    pub fn get_called_key_data(&self) -> Vec<u8> {
        let key_data: Option<Vec<u8>> = self.key_data.lock().unwrap().clone();
        key_data.unwrap()
    }

    pub fn get_called_key_name(&self) -> String {
        let key_name: Option<String> = self.key_name.lock().unwrap().clone();
        key_name.unwrap()
    }
}

pub struct MockAsymmetricKey {
    key_data: Vec<u8>,
    key_algorithm: AsymmetricKeyAlgorithm,
    result: Arc<Mutex<Result<Vec<u8>, CryptoProviderError>>>,
}

impl AsymmetricProviderKey for MockAsymmetricKey {
    fn sign(&self, _data: &[u8]) -> Result<Vec<u8>, CryptoProviderError> {
        self.result.lock().unwrap().clone()
    }
    fn get_der_public_key(&self) -> Result<Vec<u8>, CryptoProviderError> {
        self.result.lock().unwrap().clone()
    }
    fn get_key_algorithm(&self) -> AsymmetricKeyAlgorithm {
        self.key_algorithm
    }
}

impl ProviderKey for MockAsymmetricKey {
    fn delete(&mut self) -> Result<(), CryptoProviderError> {
        let result: &Result<Vec<u8>, CryptoProviderError> = &self.result.lock().unwrap();
        match result {
            Err(err) => Err(err.clone()),
            Ok(_) => Ok(()),
        }
    }
    fn get_key_data(&self) -> Vec<u8> {
        self.key_data.to_vec()
    }
    fn get_provider_name(&self) -> &'static str {
        MockProvider::new().get_name()
    }
}

pub struct MockSealingKey {
    key_data: Vec<u8>,
    result: Arc<Mutex<Result<Vec<u8>, CryptoProviderError>>>,
}

impl SealingProviderKey for MockSealingKey {
    fn encrypt(&self, _data: &[u8]) -> Result<Vec<u8>, CryptoProviderError> {
        let result: &Result<Vec<u8>, CryptoProviderError> = &self.result.lock().unwrap();
        result.clone()
    }
    fn decrypt(&self, _data: &[u8]) -> Result<Vec<u8>, CryptoProviderError> {
        let result: &Result<Vec<u8>, CryptoProviderError> = &self.result.lock().unwrap();
        result.clone()
    }
}

impl ProviderKey for MockSealingKey {
    fn delete(&mut self) -> Result<(), CryptoProviderError> {
        let result: &Result<Vec<u8>, CryptoProviderError> = &self.result.lock().unwrap();
        match result {
            Err(err) => Err(err.clone()),
            Ok(_) => Ok(()),
        }
    }
    fn get_key_data(&self) -> Vec<u8> {
        self.key_data.to_vec()
    }
    fn get_provider_name(&self) -> &'static str {
        MockProvider::new().get_name()
    }
}
