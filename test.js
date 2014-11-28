var test = require('./ConfigCenter');
console.log(test.cc_getConfigValueByKey("config_version")); 
console.log(test.cc_getConfigPortByKey("config_version"));
console.log(test.cc_getConfigValueByKeySet("config_version", 0));

