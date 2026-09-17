# nodeServer integration

Not part of the `ForceDX7` addon itself - these patch the **nodeServer**
addon (a separate, shared MockbaMod addon), which is what actually renders
the Modules page and the home-page quick-links.

## Modules page (`/moduler`) - no patch needed

Automatic: nodeServer's `moduler` endpoint (`api/endpoints/moduler/index.js`)
scans every `AddOns/*/NSMODULE.json` and lists whatever it finds. Once
`addon/NSMODULE.json` is deployed inside `AddOns/ForceDX7/`, "DX7"
just appears there.

## Home page quick-link - two files to add

1. Copy `forcedx7.js` to nodeServer's `app/api/endpoints/forcedx7.js`.
2. Add this entry to `app/api/ENDPOINTS.js`'s exported array:

```js
    {
        NAME: "DX7",
        PATH: "./api/endpoints/forcedx7.js",
        PARAM: "/forcedx7",
        URL: "/forcedx7",
        HIDDEN: false,
        HOME: true,
        TARGET: "FORCEDX7"
    },
```

3. Restart nodeServer for the new route to be picked up (a plain process
   kill+relaunch of nodeServer's own `server.js`, no `acvs`/MPC touched).

Both files target port **8307** (Force DX7's web panel - the next free slot
after force-acid's 8303, force-maze's 8304, ForceMazeSeq's 8305, and
force-jv880's 8306 - confirmed via `netstat -tnl` on the live device, not
just the ports known from local sibling repos, per force-jv880's own
port-collision lesson) - if you ever change `web/server.py --port`, update
`forcedx7.js`'s redirect target to match.
