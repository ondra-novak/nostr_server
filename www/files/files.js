class SimpleNostrClient {
    
    constructor(backup_words, relay) {

        const connect_future = (ok, error)=>{
            this.#relay.onopen = ()=>{ok();};
            this.#relay.onerror = (e)=>{error(e);};             
            this.#relay.onmessage = (e)=>{this.#process_msg(e);};
        };

        const reconnect_fn = () => {
            this.#connect_promise = new Promise((ok, error)=>{
                setTimeout(()=>{
                    this.#relay = new WebSocket(relay);
                    connect_future(ok, error);
                    this.#relay.onclose = reconnect_fn;
                },5000);
            })
        };

        this.#privKey = SimpleNostrClient.getPrivkeyHex(backup_words);
        this.#pubKey = nobleSecp256k1.getPublicKey( this.#privKey, true );
        this.#pubKeyMinus2 = this.#pubKey.substring( 2 );
        this.#relay = new WebSocket(relay);        
        this.#connect_promise = new Promise(connect_future);
        this.#relay.onclose = reconnect_fn;
    
    }

    get_pubkey() {
        return this.#pubKeyMinus2;
    }

    check_connect() {
        return this.#connect_promise;
    }


    #privKey;
    #pubKey;
    #pubKeyMinus2;
    #relay;
    #connect_promise;

    static  computeRawPrivkey( node ) {
        return bitcoinjs.ECPair.fromPrivateKey( node.privateKey, { network: bitcoinjs.networks.mainnet } );
    }
    static getPrivkeyHex( backupwords ) {
        var seed = bip39.mnemonicToSeedSync( backupwords );
        var node = bip32.fromSeed( seed );
        var path = "m/44'/1237'/0'/0/0";
        var root = node;
        var child = root.derivePath( path );
        return SimpleNostrClient.computeRawPrivkey( child ).__D.toString( 'hex' );
    }

    async sign_event(ev) {
        var now = Math.floor( Date.now() / 1000 );        
        var newevent = [
            0,
            this.#pubKeyMinus2,
            now,
            ev.kind,
            ev.tags,
            ev.content
        ];
        var message = JSON.stringify( newevent );
        var msghash = bitcoinjs.crypto.sha256( message ).toString( 'hex' );
        ev.id = msghash;
        ev.pubkey = this.#pubKeyMinus2;
        ev.created_at = now;
        ev.sig = await nobleSecp256k1.schnorr.sign( msghash, this.#privKey );
        return ev;
    }

    send_req(cmd, response_fn, bin) {
        bin = !!bin;
        if (cmd instanceof Blob) {
            console.log("Send","<binary message>", cmd.size);
            this.#relay.send(cmd);
        } else {
            console.log("Send", cmd);
            this.#relay.send(JSON.stringify(cmd));
        }
        if (response_fn) {
            return this.wait(response_fn, bin);
        }
    }
    
    wait(response_fn, bin) {
        return new Promise((ok,err)=>{
            if (response_fn) {
                this.#listeners.push({
                    bin:bin,
                    fn:response_fn,
                    ok:ok,
                    err:err
                });
            }
        });
    }
    
    #process_msg(e) {
        let msg;
        let bin = false;
        if (typeof e.data == "string") {
            msg = JSON.parse(e.data);
            console.log("Receive", msg);
        } else {
            msg = e.data;
            console.log("Receive","<binary message>",msg.size);
            bin = true;
        }

        this.#listeners = this.#listeners.filter(l=>{
            if (bin != l.bin) return true;
            try {
                let r = l.fn(msg);
                if (r === undefined) return true;
                l.ok(r);
                return false;
            } catch (e) {
                l.err(e);
                return false;
            }
        });
    }

    #listeners=[];

};

function readFile(file) {
    return new Promise((resolve, reject) => {
      let reader = new FileReader()
      reader.addEventListener("loadend", e => resolve(e.target.result))
      reader.addEventListener("error", reject)
      reader.readAsArrayBuffer(file)
    })
}

async function getAsByteArray(file) {
    return new Uint8Array(await readFile(file));
}

var app;

async function do_upload() {
    let el = document.getElementById("fileobj");
    let fdata = await getAsByteArray(el.files[0]);
    let fhex = bitcoinjs.crypto.sha256(fdata).toString("hex");
    document.getElementById("fileidout").textContent = fhex;
    let desc = document.getElementById("filedesc").value;
    let size = el.files[0].size;
    let name = el.files[0].name;
    let type = el.files[0].type;
    type = type?type:"application/octet-stream";
    let kind = document.getElementById("notekind").valueAsNumber;
    let event = {    
        content:desc,
        tags:[
            ["attachment",fhex,""+size,type]
        ],
        kind:kind
    }
    event = await app.sign_event(event);
    let status = await app.send_req(["ATTACH", event],(msg)=>{
        if (msg[0] == "OK" && msg[1]==event.id) return [msg[2],msg[3]];
        if (msg[0] == "NOTICE") return [false, msg[1]];
    });
    if (status[0]) {
        var b = new Blob([fdata], {type:type});
        status = await app.send_req(b,(msg)=>{
            if (msg[0] == "ATTACH" && msg[1] == fhex) return [msg[2],msg[3]];
        });
        if (status[0]) alert ("upload successful");
        else alert("Upload error:" + status[1]);
    } else {
        alert("Request error:" + status[1]);
    }
}

var imgurl;

async function do_fetch() {
    let fileid = document.getElementById("fileid").value;
    let status = await app.send_req(["FETCH",fileid],(msg)=>{
        if (msg[0] == "FETCH" && msg[1] == fileid) return [msg[2],msg[3]];
    });
    if (status[0]) {
        if (imgurl) URL.revokeObjectURL(imgurl);
        imgurl = null;
        imgurl = await app.wait((msg)=>{
            msg = new Blob([msg], { type: status[1] });
            var url = URL.createObjectURL(msg);            
            return url;            
        },true);
        let el = document.getElementById("outimage");
        el.hidden = false;
        el.src = imgurl;        
    } else {        
        alert(status[1]);
    }
}

async function do_link() {
    let fileid = document.getElementById("fileid").value;
    let status = await app.send_req(["LINK",fileid],(msg)=>{
        if (msg[0] == "LINK" && msg[1] == fileid) return [msg[2],msg[3]];
    });
    if (status[0]) {
        let el = document.getElementById("filelink");
        el.href = status[1];
        el.textContent = status[1];
    } else {
        alert(status[1]);
    }
}

async function start() {
    app = new SimpleNostrClient("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon",
        (location.protocol=="http:"?"ws://":"wss://")+location.hostname+":"+location.port);

    await app.check_connect();
    console.log("connected");
    document.getElementById("pubkey").textContent = app.get_pubkey();
    document.getElementById("doupload").addEventListener("click", do_upload);
    document.getElementById("dofetch").addEventListener("click", do_fetch);
    document.getElementById("dolink").addEventListener("click", do_link);
}