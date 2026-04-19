# Free-Reading-Wifi

Little bit of a messy repo, but basically how I chose to implement https://iffybooks.net/zines/ - "Spread ideas with a pocket wi-fi portal" with a little help from AI's (you'll need the DNSServer files from their repo, didn't wanna copy here)

Makes use of an SD card to hold more content (but it's super overkill) but also to make file transfer way easier. 
https://github.com/wemos/D1_mini_Examples/blob/master/examples/04.Shields/Micro_SD_Shield/listfiles/listfiles.ino was big help

### parsing.py / list.html
to help me bulk download from https://www.gutenberg.org/browse/scores/top#books-last30 but then rename them in such a way that makes the folder names:
1) somewhat identify-able (ID's are still present)
2) in an order that matches the popularity from the original site

## TODO
* Rename wifi network from "test"
* Investigate if there's other ways to get around the 8.3 name convention than the meta.txt files
* Parsing has trouble when no author, often ID gets caught in the title
* ESPAsyncWebServer might be able to provide some more parallelization to make it less slow
* add bigger description to the site
* Find a way to blink the LED when requests are made without blocking the request path
