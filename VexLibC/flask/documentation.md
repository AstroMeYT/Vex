# Flask Library v1.0.0

The Flask Vex library is a way to listen to API endpoint requests with Vex code.

## Command Reference

- Use ```flask.start(port = "<PORT>")``` to start listening on a specific port. This can be accessed at https://localhost:PORT

- The server can be stopped using ```flask.stop()```

- Use ```flask.clear(event = "EVENT-NAME")``` to set an event variable back to False after use.

- When ready to pull the next request from the queue, use ```flask.pop_request()``` to pull its data. The output:

```
{
  "id": "uuid-string-identifier",
  "method": "GET",
  "route": "endpoint/path",
  "data": "raw-body-payload-content"
}
```

- When ready to respond to a request, use ```flask.respond(req_id = reqIdVar, status = "CODE", body = 'BODY JSON', type = "application/json")```. The 'reqIdVar' is the value of the "id" key from the pop_request command. 'status' and 'body' are your response data keys (normal), and the 'type' is the response type. This is defaulted to "application/json".

## Event Listening

Using the connection between libraries and event:lib.var variables in your Vex code, you can get live variables from the server daemon that is created with the library.

- ```event:flask.<endpoint>```:  This is set to True if a trigger happens on an API endpoint of your server. These triggers are located on ```/trigger/<endpoint>```. It is best to reset these after use using ```web.clear(event = "<endpoint>")```.

- ```event:flask.api_request```: This variable is True if you have requests waiting in the queue. It will automatically be set to False if not used.

## Example Code

Below is example code using a simple two-endpoint server in Vex code.

``` vex
addlib flask as flask
addlib json as json

# Start the daemon
flask.start(port = 5000)
print("Vex API Server is running on Port 5000!")
print("Try visiting http://localhost:5000/api/hello in your browser.")

# Keep the script alive and listen for events non-blockingly
until (1 = 0)
    
    # Listen for the universal API trigger
    if (event:flask.api_request)
        
        # Pop the oldest request out of the server's queue
        globalvar req = flask.pop_request()
        
        # Use our JSON library to extract the metadata
        globalvar reqId = json.getkey(target="id", json=req)
        globalvar reqRoute = json.getkey(target="route", json=req)
        globalvar reqMethod = json.getkey(target="method", json=req)
        
        print("Received ", reqMethod, " request to /api/", reqRoute)
        
        # ---------------------------------------------
        # Route 1: /api/hello
        # ---------------------------------------------
        if (reqRoute = "hello")
            flask.respond(req_id=reqId, status=200, body='{"message": "Hello from the Vex Interpreter!"}')
        
        # ---------------------------------------------
        # Route 2: /api/status
        # ---------------------------------------------
        elseif (reqRoute = "status")
            flask.respond(req_id=reqId, status=200, body='{"status": "online", "uptime_status": "excellent"}')
            
        # ---------------------------------------------
        # Fallback (404 Not Found)
        # ---------------------------------------------
        else
            flask.respond(req_id=reqId, status=404, body='{"error": "Endpoint not found"}')
```
