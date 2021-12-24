// gzips contents of the ESP-hosted website

const DATADIR = '../firmware/data/web';
const TARGET_EXT = [".js",".css", ".html"];

var fs = require('fs');
var path = require('path');
var zlib = require('zlib');

var walk = function(dir, done) {
  var results = [];
  fs.readdir(dir, function(err, list) {
    if (err) return done(err);
    var pending = list.length;
    if (!pending) return done(null, results);
    list.forEach(function(file) {
      file = path.resolve(dir, file);
      fs.stat(file, function(err, stat) {
        if (stat && stat.isDirectory()) {
          walk(file, function(err, res) {
			results = results.concat(res);
			if (!--pending) done(null, results);
          });
        } else {
		  if (TARGET_EXT.includes(path.extname(file))) {
            results.push(file);
		  }
		  if (!--pending) done(null, results);
        }
      });
    });
  });
};

walk('./'+DATADIR, function(err, results) {
  if (err) throw err;
	
  results.forEach((filename) => {
    fs.readFile(filename, 'utf8', (err, contents) => {
      zlib.gzip(contents, (_, zContents) => {
        fs.writeFile(filename + '.gz', zContents, (error) => {
          console.log(filename);
        });
      });
    });
  });
  
});
