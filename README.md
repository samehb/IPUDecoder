# IPUDecoder
## Introduction
IPUDecoder decodes IPU videos and converts them into the M2V format. This project is based on Holger's implementation (hawkear@gmx.de). Most of the project is Holger's work. I have modified the code and enhanced it enabling support for all IPUs plus adding some fixes.

## How to Use
The IPUDecoder takes 3 arguments. Here is how you use it:
``` 
IPUDecoder input.ipu output.m2v [mode]
```
[mode] has two values depending on IPU type. It is either 0 or 1. If you skip [mode], 0 is assumed. If you use IPUDecoder with the 0 mode and receive a distorted m2v video, simply use 1, and that will take care of it. Here are some examples:
``` 
IPUDecoder 1.ipu 1.m2v
```
``` 
IPUDecoder 2.ipu 2.m2v 0
```
``` 
IPUDecoder 3.ipu 3.m2v 1
```
## Copyright
Contact Holger Kuhn (hawkear@gmx.de), if you are planning on using this project for anything that is not free.

## Links
[Blog](http://sres.tumblr.com/)
