#!/bin/bash

# export DBUS_SESSION_BUS_ADDRESS environment variable useful when the script is set as a cron job
PID=$(pgrep gnome-session | head -n 1)
export DBUS_SESSION_BUS_ADDRESS=$(grep -z DBUS_SESSION_BUS_ADDRESS /proc/$PID/environ | cut -f2- -d= | tr -d '\0')

# $bing is needed to form the fully qualified URL for
# the Bing pic of the day
bing="https://bing.com"

# $xmlURL is needed to get the xml data from which
# the relative URL for the Bing pic of the day is extracted
#
# The mkt parameter determines which Bing market you would like to
# obtain your images from.
# Valid values are: en-US, zh-CN, ja-JP, en-AU, en-UK, de-DE, en-NZ, en-CA.
#
# The idx parameter determines where to start from. 0 is the current day,
# 1 the previous day, etc.
xmlURL="http://www.bing.com/HPImageArchive.aspx?format=xml&idx=1&n=1&mkt=en-US"

userAgent="User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_10_3) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/44.0.2403.89 Safari/537.36"

# $saveDir is used to set the location where Bing pics of the day
# are stored.  $HOME holds the path of the current user's home directory
saveDir="$HOME/Pictures/BingWallpapers/"
mkdir -p $saveDir;

# Set picture options
# Valid options are: none,wallpaper,centered,scaled,stretched,zoom,spanned
picOpts="zoom"

# The desired Bing picture resolution to download
# Valid options: "_1024x768" "_1280x720" "_1366x768" "_1920x1200"
desiredPicRes="_1920x1200"

# The file extension for the Bing pic
picExt=".jpg"

lastDownloadedFile="/tmp/wallpaper_download_time.txt"

# the old wallpaper
old=$(gsettings get org.gnome.desktop.background picture-uri | tr -d "'")
cleanold="${old/file:\/\//}"
lastWallpaper=$(ls "$saveDir" -Art | tail -n 1)
lastWallpaperFilename=$(basename -- "$lastWallpaper")

function set_wallpaper {
	picName=$1
	fileurl=$(mktemp --suffix=$picName)

	# write the tasks
	convert "$saveDir$picName" \
		-font FreeMono \
		-fill white \
		-stroke black \
		-pointsize 20 \
		-gravity east \
		-annotate +100+0 \
		"$(task list)" \
		"${fileurl}";

	# Set the GNOME3 wallpaper
	gsettings set org.gnome.desktop.background picture-uri "file://$fileurl"
	gsettings set org.gnome.desktop.background picture-uri-dark "file://$fileurl"

	# Set the GNOME 3 wallpaper picture options
	gsettings set org.gnome.desktop.background picture-options $picOpts
	
	if [[ "$cleanold" == "/tmp/"* ]]; then
		rm -f "$cleanold"
	fi;
}

# Create saveDir if it does not already exist
mkdir -p $saveDir

# setting the background image before it's loaded
if [ -f "$lastWallpaper" ] && [ ! -f "${cleanold}" ]; then
	set_wallpaper $lastWallpaper
fi;


# if 1 hour(s) has been passed since last download
if [ ! -f "$saveDir$lastWallpaper" ] ||
   [ ! -f "$lastDownloadedFile" ] ||
   [ $(expr $(date +%s) - $(cat "$lastDownloadedFile")) -gt 3600 ]; then

	# Extract the relative URL of the Bing pic of the day from
	# the XML data retrieved from xmlURL, form the fully qualified
	# URL for the pic of the day, and store it in $picURL

	# Form the URL for the desired pic resolution
	desiredPicURL=$bing$(echo $(curl -L -H "$userAgent" -s $xmlURL) | grep -oP "<urlBase>(.*)</urlBase>" | cut -d ">" -f 2 | cut -d "<" -f 1)$desiredPicRes$picExt

	# Form the URL for the default pic resolution
	defaultPicURL=$bing$(echo $(curl -L -H "$userAgent" -s $xmlURL) | grep -oP "<url>(.*)</url>" | cut -d ">" -f 2 | cut -d "<" -f 1)

  echo $(curl -s $xmlURL)
  echo xml url: $xmlURL
  echo desiredPicURL: $desiredPicURL
  echo defaultPicURL: $defaultPicURL

	# $picName contains the filename of the Bing pic of the day

	# Attempt to download the desired image resolution. If it doesn't
	# exist then download the default image resolution

  if (wget --user-agent="$userAgent" --quiet --spider "$desiredPicURL")
	then

		# Set picName to the desired picName
		picName=${desiredPicURL##*/}
		# Download the Bing pic of the day at desired resolution
		curl -L -H "$userAgent" -s -o "$saveDir$picName" "$desiredPicURL"
	else
		# Set picName to the default picName
		picName=${defaultPicURL##*/}
		# Download the Bing pic of the day at default resolution
		curl -L -H "$userAgent" -s -o "$saveDir$picName" "$defaultPicURL"
	fi

	set_wallpaper $picName;
	
	# Update last downloaded time
	date +%s > "$lastDownloadedFile";

	# Remove pictures older than 30 days
	find $saveDir -atime 30 -delete

## upldate the last image
else
	set_wallpaper "$lastWallpaperFilename"
fi;

# Exit the script
exit

