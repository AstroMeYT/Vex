# Fetch Library v1.1.0

The fetch library is a Vex library designed to connect your code to the outside world. It allows making requests using GET, POST, PATCH, PUT, and DELETE. The backend uses cURL. Below is example code for each request type:

```
addlib fetch as fetch

fetch.get(url='https://example.com')     # using GET to fetch a URL
fetch.post(url='https://example.com', data='DATA HERE')     # using POST to interact with a URL
fetch.patch(url='https://example.com', data='DATA HERE')     # using PATCH to interact with a URL
fetch.put(url='https://example.com', data='DATA HERE')     # using PUT to interact with a URL
fetch.delete(url='https://example.com', data='DATA HERE')     # using DELETE to interact with a URL
```

The code above runs the commands, but it doesn't give the output back to the Vex code. To do so, you must set a variable to the output, as described in the Vex Documentation:

```
globalvar myVar = fetch.get(url='https://example.com')     # Or you can use the other options too
```
