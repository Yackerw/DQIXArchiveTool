// GP2 Web UI logic (no network IO; everything local)
(function(){
  'use strict';

  const byId = (id) => document.getElementById(id);
  const dropzone = byId('dropzone');
  const fileInput = byId('fileInput');
  const fileList = byId('fileList');
  const btnZipAll = byId('btnZipAll');
  const btnClearExport = byId('btnClearExport');
  const btnClearLog = byId('btnClearLog');
  const btnSaveLog = byId('btnSaveLog');
  const selTheme = byId('selTheme');
  const selLang = byId('selLang');

  function sanitizeName(name){
    return (name||'').replace(/\\/g,'/').replace(/\.+\//g,'').replace(/^\/+/, '');
  }

  function ensureDir(path) {
    // Recursively ensure directories exist in MEMFS
    const parts = sanitizeName(path).split('/');
    let accum = '';
    for (let i=0;i<parts.length-1;i++){
      const p = parts[i];
      if (!p) continue;
      accum += '/' + p;
      try { Module.FS.mkdir(accum); } catch(_) {}
    }
  }

  async function processFile(file){
    const arrayBuf = await file.arrayBuffer();
    const u8 = new Uint8Array(arrayBuf);
    const inPath = '/input/' + sanitizeName(file.name);
    ensureDir(inPath);
    Module.FS.writeFile(inPath, u8);
    Module.ccall('ProcessFile', null, ['string'], [inPath]);
    refreshExportList();
  }

  function walkDir(path){
    // Recursively list files under path
    const results = [];
    function walk(p){
      let entries = [];
      try { entries = Module.FS.readdir(p); } catch(_) { return; }
      for (const name of entries){
        if (name === '.' || name === '..') continue;
        const full = (p === '/' ? '' : p) + '/' + name;
        let stat;
        try { stat = Module.FS.stat(full); } catch(_) { continue; }
        if (Module.FS.isDir(stat.mode)) {
          walk(full);
        } else if (Module.FS.isFile(stat.mode)) {
          results.push({ path: full, size: stat.size });
        }
      }
    }
    walk(path);
    return results.sort((a,b)=> a.path.localeCompare(b.path));
  }

  function downloadFile(path){
    const data = Module.FS.readFile(path);
    const blob = new Blob([data], {type:'application/octet-stream'});
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = path.replace(/^\//,'');
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(()=>URL.revokeObjectURL(a.href), 1000);
  }

  function refreshExportList(){
    const files = walkDir('/export');
    fileList.innerHTML = '';
    if (!files.length){
      fileList.classList.add('empty');
      fileList.setAttribute('data-i18n','noOut');
      fileList.textContent = t('noOut');
      btnZipAll.disabled = true;
      return;
    }
    btnZipAll.disabled = false;
    fileList.classList.remove('empty');
    const ul = document.createElement('ul');
    ul.className = 'list';
    for (const f of files){
      const li = document.createElement('li');
      li.className = 'list-item';
      // Expander or spacer
      const expWrap = document.createElement('div');
      expWrap.className = 'expander';
      const isNarcFile = isNarc(f.path);
      let narcState = { decoded: false, entries: null, container: null };
      if (isNarcFile){
        const btnExp = document.createElement('button');
        btnExp.setAttribute('aria-label', t('expand'));
        const chev = document.createElement('span');
        chev.className = 'chev';
        chev.textContent = '>';
        btnExp.appendChild(chev);
        btnExp.addEventListener('click', async ()=>{
          const expanded = li.classList.toggle('expanded');
          btnExp.setAttribute('aria-label', expanded? t('collapse'): t('expand'));
          if (expanded){
            if (!narcState.decoded){
              // Decode once
              const entries = await decodeNarc(f.path);
              narcState.decoded = true;
              narcState.entries = entries;
            }
            if (!narcState.container){
              narcState.container = renderNarcContents(narcState.entries, f.path);
              li.appendChild(narcState.container);
            } else {
              narcState.container.classList.remove('hidden');
            }
          } else {
            if (narcState.container) narcState.container.classList.add('hidden');
          }
        });
        expWrap.appendChild(btnExp);
      } else {
        const spacer = document.createElement('div');
        spacer.style.minWidth = '24px';
        spacer.setAttribute('aria-hidden','true');
        expWrap.appendChild(spacer);
      }

      const name = document.createElement('span');
      name.className = 'name';
      name.textContent = f.path.replace(/^\/export\//,'');
      const size = document.createElement('span');
      size.className = 'size';
      size.textContent = f.size;
      const btn = document.createElement('button');
      btn.textContent = t('download');
      btn.className = 'btn';
      btn.addEventListener('click', ()=> downloadFile(f.path));
      // Per-NARC: all download button
      const btnAll = document.createElement('button');
      btnAll.textContent = t('downloadAll');
      btnAll.className = 'btn';
      btnAll.style.display = isNarcFile? '' : 'none';
      btnAll.disabled = false; // will decode on demand
      btnAll.addEventListener('click', async ()=>{
        const entries = await decodeNarc(f.path);
        const pairs = entries.map(e=>({ name: e.name, data: e.data }));
        const zipU8 = buildZipStore(pairs);
        const blob = new Blob([zipU8], {type:'application/zip'});
        const a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        const base = (name.textContent||'narc').replace(/\.[^./\\]+$/,'');
        a.download = base + '.narc.zip';
        document.body.appendChild(a);
        a.click(); a.remove();
        setTimeout(()=>URL.revokeObjectURL(a.href), 1000);
      });
      li.appendChild(expWrap);
      li.appendChild(name);
      li.appendChild(size);
      li.appendChild(btn);
      li.appendChild(btnAll);
      ul.appendChild(li);
    }
    fileList.appendChild(ul);
  }

    // NARC helpers
    const narcCache = new Map(); // path -> [{name,data(U8)}]
    function isNarc(path){
        try{
            const u8 = Module.FS.readFile(path);
            return u8.length >= 4 && u8[0]===0x4E && u8[1]===0x41 && u8[2]===0x52 && u8[3]===0x43; // 'NARC'
        }catch(_){ return false; }
    }
    function sanitizeZipName(name){
        return (name||'file.bin').replace(/^[\\/]+/,'').replace(/\.+\//g,'');
    }
  async function decodeNarc(path){
    if (narcCache.has(path)) return narcCache.get(path);
    const data = Module.FS.readFile(path);
    let entries = [];
    try{
      const narc = Narc.load(data);
      for (let i=0;i<narc.files.length;i++){
        const nm = narc.fnt.getFilenameOf(i) || ('file'+i+'.bin');
        const safe = sanitizeZipName(nm);
        const bytes = narc.files[i];
        entries.push({ name: safe, data: bytes });
      }
    }catch(e){
      console.error('NARC decode failed:', e);
    }
    narcCache.set(path, entries);
    return entries;
  }

  function renderNarcContents(entries, parentPath){
    const wrap = document.createElement('div');
    wrap.className = 'narc-contents';
    const list = document.createElement('ul');
    list.className = 'narc-list';
    for (const e of entries){
      const row = document.createElement('li');
      row.className = 'narc-row';
      const nm = document.createElement('span');
      nm.textContent = e.name;
      const sz = document.createElement('span');
      sz.className = 'size';
      sz.textContent = e.data.length;
      const b = document.createElement('button');
      b.className = 'btn';
      b.textContent = t('download');
      b.addEventListener('click', ()=>{
        const blob = new Blob([e.data], {type:'application/octet-stream'});
        const a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        a.download = e.name;
        document.body.appendChild(a); a.click(); a.remove();
        setTimeout(()=>URL.revokeObjectURL(a.href), 1000);
      });
      row.appendChild(nm);
      row.appendChild(sz);
      row.appendChild(b);
      list.appendChild(row);
    }
    wrap.appendChild(list);
    return wrap;
  }

  function crc32(buf){
    // Minimal CRC32 for ZIP store mode
    const table = (function(){
      const t = new Uint32Array(256);
      for (let i=0;i<256;i++){
        let c = i;
        for (let j=0;j<8;j++) c = (c & 1) ? (0xEDB88320 ^ (c>>>1)) : (c>>>1);
        t[i] = c>>>0;
      }
      return t;
    })();
    let crc = 0xFFFFFFFF;
    for (let i=0;i<buf.length;i++) crc = table[(crc ^ buf[i]) & 0xFF] ^ (crc >>> 8);
    return (crc ^ 0xFFFFFFFF) >>> 0;
  }

  function strToU8(s){
    const u = new Uint8Array(s.length);
    for (let i=0;i<s.length;i++) u[i] = s.charCodeAt(i) & 0xFF;
    return u;
  }

  function le32(n){
    const b = new Uint8Array(4); const v = new DataView(b.buffer);
    v.setUint32(0, n>>>0, true); return b;
  }
  function le16(n){
    const b = new Uint8Array(2); const v = new DataView(b.buffer);
    v.setUint16(0, n & 0xFFFF, true); return b;
  }

  function concat(parts){
    let len = 0; for (const p of parts) len += p.length;
    const out = new Uint8Array(len);
    let off = 0; for (const p of parts){ out.set(p, off); off += p.length; }
    return out;
  }

  function buildZipStore(files){
    // files: array of {name, data(Uint8Array)}
    const localRecs = [];
    const central = [];
    let offset = 0;
    for (const f of files){
      const nameU8 = strToU8(f.name);
      const crc = crc32(f.data);
      const size = f.data.length;
      const local = concat([
        strToU8('\x50\x4b\x03\x04'),        // local file header sig
        le16(20),                               // version
        le16(0),                                // flags
        le16(0),                                // method: store
        le16(0), le16(0),                       // mtime, mdate
        le32(crc),
        le32(size), le32(size),                 // sizes
        le16(nameU8.length), le16(0),           // name len, extra len
        nameU8,
        f.data
      ]);
      localRecs.push(local);

      const centralRec = concat([
        strToU8('\x50\x4b\x01\x02'),        // central header sig
        le16(20), le16(20),                     // version made/by
        le16(0),                                // flags
        le16(0),                                // method
        le16(0), le16(0),                       // mtime,mdate
        le32(crc),
        le32(size), le32(size),
        le16(nameU8.length), le16(0), le16(0), // name, extra, comment
        le16(0), le16(0),                       // disk, attrs
        le32(0),                                // external attrs
        le32(offset),                           // local header offset
        nameU8
      ]);
      central.push(centralRec);
      offset += local.length;
    }
    const centralBlob = concat(central);
    const total = concat(localRecs.concat([centralBlob, concat([
      strToU8('\x50\x4b\x05\x06'),          // end of central dir
      le16(0), le16(0),                         // disk numbers
      le16(files.length), le16(files.length),   // total entries
      le32(centralBlob.length),                 // size of central dir
      le32(localRecs.reduce((s,b)=>s+b.length,0)), // offset of central dir
      le16(0)                                   // comment length
    ])]));
    return total;
  }

  function zipAll(){
    const files = walkDir('/export');
    if (!files.length) return;
    const pairs = files.map(f=>({ name: f.path.replace(/^\//,'').replace(/^export\//,''), data: Module.FS.readFile(f.path) }));
    const zipU8 = buildZipStore(pairs);
    const blob = new Blob([zipU8], {type:'application/zip'});
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'export.zip';
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(()=>URL.revokeObjectURL(a.href), 1000);
  }

  function clearExport(){
    // Recursively remove /export
    function rm(path){
      let entries = [];
      try { entries = Module.FS.readdir(path); } catch(_) { return; }
      for (const name of entries){
        if (name === '.' || name === '..') continue;
        const full = path + '/' + name;
        let st; try { st = Module.FS.stat(full); } catch(_) { continue; }
        if (Module.FS.isDir(st.mode)) {
          rm(full);
          try { Module.FS.rmdir(full); } catch(_) {}
        } else {
          try { Module.FS.unlink(full); } catch(_) {}
        }
      }
    }
    rm('/export');
    try { Module.FS.rmdir('/export'); } catch(_) {}
    try { Module.FS.mkdir('/export'); } catch(_) {}
    try { Module.FS.mkdir('/tmp'); } catch(_) {}
    narcCache.clear();
    refreshExportList();
  }

  function autoResetBeforeImport(){
    const hasOut = walkDir('/export').length > 0;
    if (hasOut){
      clearExport();
    } else {
      narcCache.clear();
    }
  }

  function setupDnD(){
    function stop(e){ e.preventDefault(); e.stopPropagation(); }
    ['dragenter','dragover','dragleave','drop'].forEach(ev=>{
      dropzone.addEventListener(ev, stop, false);
    });
    dropzone.addEventListener('dragover', ()=> dropzone.classList.add('hover'));
    dropzone.addEventListener('dragleave', ()=> dropzone.classList.remove('hover'));
    dropzone.addEventListener('drop', async (e)=>{
      dropzone.classList.remove('hover');
      const files = e.dataTransfer.files;
      autoResetBeforeImport();
      for (const f of files) await processFile(f);
    });
    fileInput.addEventListener('change', async (e)=>{
      const files = e.target.files;
      autoResetBeforeImport();
      for (const f of files) await processFile(f);
      fileInput.value = '';
    });
  }

  function setupActions(){
    btnZipAll.addEventListener('click', zipAll);
    btnClearExport.addEventListener('click', clearExport);
    btnClearLog.addEventListener('click', ()=>{ byId('log').value=''; });
    btnSaveLog.addEventListener('click', ()=>{
      const text = byId('log').value;
      const blob = new Blob([text], {type:'text/plain'});
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = 'log.txt';
      document.body.appendChild(a);
      a.click(); a.remove();
      setTimeout(()=>URL.revokeObjectURL(a.href), 1000);
    });
    if (selTheme){
      selTheme.addEventListener('change', ()=>{
        try{ localStorage.setItem('ui.theme', selTheme.value); }catch(_){ }
        document.documentElement.setAttribute('data-theme', getTheme());
      });
    }
    if (selLang){
      selLang.addEventListener('change', ()=>{
        try{ localStorage.setItem('ui.lang', selLang.value); }catch(_){ }
        document.documentElement.setAttribute('lang', getLang());
        applyThemeAndLangUI();
        refreshExportList();
      });
    }
  }

  window.appInit = function(){
    try { Module.FS.mkdir('/input'); } catch(_) {}
    try { Module.FS.mkdir('/export'); } catch(_) {}
    applyThemeAndLangUI();
    setupDnD();
    setupActions();
    refreshExportList();
  };
})();
