#pragma once

#include <string>

namespace vynx_http {

/// Configuration for TLS connections.
struct tls_config {
    std::string ca_bundle_path;   ///< Path to CA bundle file. Empty = system defaults.
    bool verify_peer = true;      ///< Verify server certificate.
    bool verify_hostname = true;  ///< Verify hostname against certificate.
};

}  // namespace vynx_http
