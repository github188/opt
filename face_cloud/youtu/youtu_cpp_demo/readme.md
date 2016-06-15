TencentYouTu Demo Rely On Libuv
=====================================

This project is a modification copy of: 
[TencentYouTu](https://github.com/TencentYouTu/Cplusplus_sdk)

TencentYoutu use openssl crypto algorithm (HMCAC(EVP_sh1(),xxx)) for signing to the cloud.
And it use libcurl to communicate with the cloud via HTTP.

The modification copy give a replace hmac_sha1 implementation.

The modification copy rely on customers implementation of HTTP, here we give a demo rely on libuv.

building system using [makeconfigure] `An private gcc make tools` for test.
