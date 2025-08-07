# gipMultiplayer

`gipMultiplayer` is a plugin for GlistApp projects and GlistEngine games that will help you set up a local server of your own in order to establish multiplayer gameplay. It utilizes the `zNet` library. 

## 🛠️ Setup Instructions

Before running this GlistApp project, you **must** set up the `gipMultiplayer` plugin and initialize its submodules to fetch the required zNet dependencies.

### Step 1: Clone `gipMultiplayer` into your `myglistplugins` directory

```bash
cd path/to/your/glistplugins
git clone https://github.com/GlistPlugins/gipMultiplayer.git
```

### Step 2: Initialize submodules (for zNet)

```bash
cd gipMultiplayer
git submodule update --init --recursive
```
> ⚠️ Skipping this step will cause missing zNet dependencies, and the project will not run correctly.

### Step 3: Inside your GlistApp project's CMakeLists.txt file, add gipMultiplayer to your plugins.

### Step 4: Create your server.

- To initialize your local server, you can inspect the examples within the examples directory of this repository.
- You should set the IP of the server to your local IP as shown in the gCanvas.cpp file in the examples directory.
- For a host, you should set the isHost variable to 'true' before the initialization. For clients, set it to 'false'.
- For testing, you can do it by starting 2 instances, one as a host, and another as a client.
- In the examples, there is a Player class that controls a 3D cube object. The packets in the example code is able to properly display the host's and client's cubes in the same server.

## Final Notes

Make sure to run server app first and then client.
