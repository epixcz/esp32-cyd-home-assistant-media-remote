#if defined(HAS_SSL)
#if defined(SSL_AXTLS) || defined(ESP32)
    if(_client.isSSL && SSL_FINGERPRINT_IS_SET) {
        if(!_client.ssl->verify(_fingerprint.c_str(), _host.c_str())) {
            DEBUG_WEBSOCKETS("[WS-Client] certificate mismatch\n");
            WebSockets::clientDisconnect(&_client, 1000);
            return;
        }
#else
    if(_client.isSSL && SSL_FINGERPRINT_IS_SET) {
#endif
    } else if(_client.isSSL && !_CA_cert) {
#if defined(SSL_BARESSL)
        _client.ssl->setInsecure();
#endif
    }
#endif

    // send Header to Server
    sendHeader(&_client);
