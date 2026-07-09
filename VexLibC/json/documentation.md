# JSON Library v1.0.0

The JSON Vex Library allows you to manage JSON data, including getting and setting keys. The Setkey allows adding or setting a key in raw JSON data, and it returns the new raw data. The Getkey allows getting a key from raw JSON, and it returns the key's data.

```
addlib json as json

globalvar theKey = json.getkey(target = 'TARGET KEY', json = 'RAW JSON DATA')     # using getkey to parse a key from basic JSON
globalvar rawData = json.setkey(target = 'TARGET KEY', value = "KEY VALUE', json = 'RAW JSON DATA')     # using getkey to parse a key from basic JSON
```

If you want to create a string of JSON, the best way is to stack setting the same variable using Setkey, which will also create a key if one doesn't exist. 

```
globalvar rawData = "{}"
globalvar rawData = json.setkey(target = "key1", value = "data1", json = rawData)
globalvar rawData = json.setkey(target = "key2", value = "data2", json = rawData)
globalvar rawData = json.setkey(target = "key3", value = "data3", json = rawData)
```

rawData is set to: '{"key1": "data1", "key2": "data2", "key3": "data3"}'
