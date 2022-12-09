const fs = require('fs');
const { pack } = require("./node_modules/texture-compressor/dist/cli/lib/index")

var path = "./pbr/rust";
fs.readdir(path, function (err, files) {
    files.forEach(function (file) {
        // if(file.endsWith("meta"))
            // console.log(path + "/" + file.substring(0,file.length-3) + "ktx");
        var _path;
        if (path.endsWith('/')) {
            _path = path + file;
        }
        else {
            _path = path + "/" + file;
        }
        if (file.endsWith("png")||file.endsWith("jpg")||file.endsWith("jpeg")) {
            pack({
                type: 's3tc',
                input: _path,
                output: path + "/" + file.substring(0,file.length-3) + "ktx",
                compression: "DXT1",
                quality: "normal"
                // verbose: true,
            });
            console.log("done");
        }
    });
});