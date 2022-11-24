const fs = require('fs');
const ws = require('node:stream/web');

async function getChannels() {
    return await fetch("https://graph.telewebion.com/graphql", {
        "headers": {
        "accept": "application/json, text/plain, */*",
        "accept-language": "en-US,en;q=0.9",
        "content-type": "application/json",
        "sec-ch-ua": "\"Chromium\";v=\"107\", \"Not=A?Brand\";v=\"24\"",
        "sec-ch-ua-mobile": "?0",
        "sec-ch-ua-platform": "\"Linux\"",
        "sec-fetch-dest": "empty",
        "sec-fetch-mode": "cors",
        "sec-fetch-site": "same-site",
        "Referer": "https://telewebion.com/",
        "Referrer-Policy": "strict-origin-when-cross-origin"
        },
        "body": "{\"operationName\":\"getChannels\",\"variables\":{},\"query\":\"query getChannels($NumOfItems: Int) @cacheControl(maxAge: 600) {\\n  queryChannel(\\n    first: $NumOfItems\\n    filter: {product: {alloftext: \\\"tw\\\"}, enable: true}\\n    order: {asc: priority}\\n  ) {\\n    ChannelID\\n    priority\\n    name\\n    descriptor\\n    description\\n    name_english\\n    image_name\\n    has_archive\\n    type {\\n      descriptor\\n      name\\n    }\\n  }\\n}\\n\"}",
        "method": "POST"
    }).then(res => res.json());
}

function reformat_name(name) {
    return name
        .replace("channel", "tv");
}

async function download_file(url, path) {
    const res = await fetch(url);
    const fsStream = fs.createWriteStream(path);
    return res.body.pipeTo(new ws.WritableStream({
        write(chunk) {
            fsStream.write(chunk);
        }
    }));
}



async function main(channels) {
    let all_images = [];
    for (let id in channels) {
        const ch = channels[id];
        console.log("Generating TV Channel desktop", ch.name_english);
        try {
            const image_url = `https://static.telewebion.com/channelsLogo/${ch.image_name}/default`;
            const icon_path = `./tmp/icon-${ch.descriptor}.png`
            const file = `./tmp/${ch.descriptor}.desktop`;
            const name = reformat_name(ch.name_english);
            const content = `[Desktop Entry]
Version=1.0
Name=${name}
Exec=/usr/bin/vlc https://ncdn.telewebion.com/${ch.descriptor}/live/playlist.m3u8
Icon=tv-${ch.descriptor}
Terminal=false
Type=Application
Categories=AudioVideo;Video
Keywords=${ch.name_english};${ch.descriptor};${ch.name};`;

            // download the icon
            all_images.push(download_file(image_url, icon_path));

            // write the desktop file
            fs.writeFileSync(file, content);
        } catch (err) {
            console.error(err);
        }
    }
    Promise.all(all_images);
}


getChannels()
    .then(channels => channels.data.queryChannel)
    .then(main)
    .catch(console.error);
