Steps that made it work on my system (Debian 12 with NVIDIA 3060 Laptop GPU and Kinect Azure):
- make sure the Nvidia GPU is used by the system (e.g. using `envycontrol -s nvidia` on systems with hybrid graphics)
- plug in the Kinect Azure
- copy 99-k4a.rules to /etc/udev/rules.d/
- reload/apply udev rules
- create group 'plugdev' for kinect access
- add user to that group
- relogin/reboot for group changes to take effect
- [install the nvidia container toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html)
- cd into ``server_native`` and run ``../run.sh build`` to build the docker image
- run ``./run.sh up`` to start the server (optional --debug flag for debug mode and/or --shell to get a shell into the container)
- on your host machine run the kinect_access_client to access the kinect data
- to stop the server do Ctrl+C in the terminal where the server is running

> NOTE: In this project I used:
> - several online references (as well as MS Copilot and ChatGPT generated code/scripts) 
> - adapted several sources/comments/issues/solutions from websites like GitHub, StackOverflow, and others
> 
> I didn't keep track nor document any of them.
> 
> My solution may not work for others, but Issues and PRs are welcome.
> 
> I don't claim any ownership of the used code parts and scripts.
>
> This is just a personal documentation of what worked for me and is publicly shared in case it can be of help to others.
