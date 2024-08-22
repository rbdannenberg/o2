Work in Progress.


In the o2host folder, find the o2host application in the Debug folder and launch it. 

Create a configuration and run o2host. For the configuration, choose an ensemble name and a port. 
- what do all the parts of configuration mean

Create a directory named www for your files, and store any project files there, including index.html, index.js, and o2ws.js.

In your Javascript code, to initialize the o2host, you must call o2ws_initialize(param) with the ensemble name you chose earlier as the parameter. 
Assuming you have chosen an area in your configuration wide enough, this will initialize the o2host with that name for actions on the browser side. 



This application is an O2lite websocket monitor. 
The user connects to an O2host and then can create services and send messages to other services.
To see a list of all services connected to the o2host, the user can simply click Discover Services, and all running services will be displayed along with their status. 

Each remote service connected to the o2host can be tapped, meaning all future messages sent to that service will be forwarded to the local service that tapped it. 
No messages sent before tap was enabled will be shown.
The user can send messages from one service to another, local or remote. 
For sending messages, the user has the option to specify an address for the service. The user must specify the typespec for the message to be sent. 
Then, the message must be inputted with commas between each input.





