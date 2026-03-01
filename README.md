# PS4-Websrv

PS4 Websrv is a basic local HTTP server which hosts a website on the PS4. Website files hosted on `/data/websrv`.

Why use it? This allows you to load payloads through GoldHEN's PayLoader on the fly using any external device such as your phone or PC.

## PS4 Websrv Setup
### Option 1: Online method
1. Load the payload using [PSFree Enhanced](https://arabpixel.github.io/PSFree-Enhanced)

- The payload will automatically create `/data/websrv` for you if its not already and will create a basic `index.html` file be able to load couple payloads.
Thats it.

### Option 2: Payload guest
1. Download [PS4-Websrv](https://github.com/ArabPixel/ps4-websrv/releases) from latest release.
2. Put the payload inside `/data/payloads` using any FTP client or an exfat formatted USB and File Xplorer 2.0 by Lapy

3. Open Payload Guest and click on ps4-websrv. That's it.

## Usage
You can access the web server by typing your PS4 Ip on any device that's connected to the same network. Clicking on the payloads will load them immediately on the PS4.

- Since this is a HTTP server, this can be repurposed to anything web related and not just loading payloads.
- To change the design or core logic, drop your files inside `/data/websrv/`.

## How to compile
1. Setup [ps4-payload-sdk](https://github.com/Scene-Collective/ps4-payload-sdk/) if you didn't already.

2. Clone the repo and make
```bash
git clone https://github.com/ArabPixel/ps4-websrv
cd ps4-websrv && make
```
ps4-websrv.bin will be compiled in the same directory.

## Notices
- AI was used when making this payload to speedup the proccess.
- The payload is provided 'as is' and without a guarantee that'll work.