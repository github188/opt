
YunTuSDK based on TencentYouTu
===================================================
-------------

Introduction
-------------

This project is a modification copy of: 
https://github.com/TencentYouTu/Cplusplus_sdk

TencentYoutu use openssl crypto algorithm (HMCAC(EVP_sh1(),xxx)) for signing to the cloud.
And it use libcurl to communicate with the cloud via HTTP.

The modification copy give a replace hmac_sha1 implementation.

The modification copy rely on customers implementation of HTTP, here we give a demo rely on libuv.

Compiling system using CMAKE

技术
-------------