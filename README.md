<p align="center">
  <img src="docs/logo.svg" width="420" alt="vynx_http">
</p>

<br>

<p align="center">
  <em>High-performance C++ HTTP/1.1 + TLS client</em>
</p>

<br>

<p align="center">
  <a href="https://github.com/mra1k3r0/vynx_http/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/mra1k3r0/vynx_http/ci.yml?branch=master&label=CI&style=for-the-badge" alt="CI"></a>
  <img src="https://img.shields.io/badge/C%2B%2B-20-blue?logo=cplusplus&style=for-the-badge" alt="C++20">
  <img src="https://img.shields.io/badge/OpenSSL-4.0.0-green?style=for-the-badge" alt="OpenSSL">
  <img src="https://img.shields.io/badge/tests-181%2F181-brightgreen?style=for-the-badge" alt="Tests">
  <img src="https://img.shields.io/badge/license-MIT-gray?style=for-the-badge" alt="MIT">
</p>

<br>

> [!WARNING]
> **Work in progress**
>
> This library is under active development and **not yet production-ready**. APIs may change without notice.

<br>

---

> *Built from scratch. No libcurl. No Boost.Beast. No cpp-httplib. No shortcuts.*
> *A modern C++20 HTTP client with TLS, connection pooling, and a zero-allocation core — engineered for performance, not assembled from dependencies.*

<br>

### &gt; Quickstart

```cpp
#include <vynx_http/http_client.h>

using namespace vynx_http;

int main() {
    event_loop loop;
    http_client client(loop);

    auto res = client.get("https://httpbin.org/get");
    if (res.ok()) {
        printf("status: %d\n", res.value().status_code);
    }
}
```

<br>

### &gt; Build

```
requires: c++20 · mingw g++ 15.2.0 · openssl 4.0.0
```

```bash
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
cmake --build .
```

<br>

---

### License

Released under the <a href="https://github.com/mra1k3r0/vynx_http/blob/master/LICENSE"><strong>MIT License</strong></a>. Free to use, modify, and distribute.
