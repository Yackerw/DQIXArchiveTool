class FntFolder {
    constructor(name = ''){
        this.name = name;
        this.folders = {};  // {name: FntFolder}
        this.files = [];    // [filename, ...]
        this.firstId = 0;
    }

    // パスからファイルIDを取得
    getIdOf(path){
        const parts = path.split('/').filter(p => p);
        return this._findIdInPath(parts, this);
    }

    _findIdInPath(parts, folder){
        if(parts.length === 0) return -1;
        const name = parts[0];
        if(parts.length === 1){
            const idx = folder.files.indexOf(name);
            return idx >= 0 ? folder.firstId + idx : -1;
        }
        const sub = folder.folders[name];
        return sub ? this._findIdInPath(parts.slice(1), sub) : -1;
    }

    // ファイルIDからパスを取得
    getFilenameOf(id){
        const result = this._findFileById(id, [], this);
        return result ? result.join('/') : null;
    }

    _findFileById(id, path, folder){
        if(id >= folder.firstId && id < folder.firstId + folder.files.length){
            return [...path, folder.files[id - folder.firstId]];
        }
        for(const [name, sub] of Object.entries(folder.folders)){
            const result = this._findFileById(id, [...path, name], sub);
            if(result) return result;
        }
        return null;
    }

    // ファイル追加
    addFile(name){
        this.files.push(name);
    }

    // サブフォルダ追加
    addFolder(name){
        if(!this.folders[name]){
            this.folders[name] = new FntFolder(name);
        }
        return this.folders[name];
    }

    // フォルダ数カウント
    countFolders(){
        let count = 1;
        for(const sub of Object.values(this.folders)){
            count += sub.countFolders();
        }
        return count;
    }
}

class Fnt {
    // FNTデータから読み込み
    static load(fntData){
        return Fnt._loadFolder(fntData, 0xF000, 'root');
    }

    static _loadFolder(fntData, folderId, name){
        const r = new BinReader(fntData);
        const folder = new FntFolder(name);

        const offset = 8 * (folderId & 0xFFF);
        r.setPos(offset);
        const entriesOffset = r.getLInt();
        const fileId = r.getLShort();
        folder.firstId = fileId;

        r.setPos(entriesOffset);

        while(true){
            const control = r.getByte();
            if(control === 0) break;

            const length = control & 0x7F;
            const isFolder = control & 0x80;
            const name = r.readString(length);

            if(isFolder){
                const subFolderId = r.getLShort();
                folder.folders[name] = Fnt._loadFolder(fntData, subFolderId, name);
            } else {
                folder.files.push(name);
            }
        }

        return folder;
    }

    // FNTデータを生成
    static save(root){
        const folderEntries = new Map();
        let nextFolderId = 0xF000;
        const rootParentId = root.countFolders();

        function parseFolder(folder, parentId){
            const folderId = nextFolderId++;
            const entriesTable = new BinWriter();

            for(const file of folder.files){
                if(file.length > 127) throw new Error(`Filename too long: ${file}`);
                entriesTable.putByte(file.length);
                entriesTable.putAscii(file);
            }

            for(const [name, sub] of Object.entries(folder.folders)){
                const subId = parseFolder(sub, folderId);
                if(name.length > 127) throw new Error(`Folder name too long: ${name}`);
                entriesTable.putByte(name.length | 0x80);
                entriesTable.putAscii(name);
                entriesTable.putLShort(subId);
            }

            entriesTable.putByte(0);
            folderEntries.set(folderId, {
                fileId: folder.firstId,
                parentId: parentId,
                entry: entriesTable.toUint8()
            });

            return folderId;
        }

        parseFolder(root, rootParentId);

        const w = new BinWriter();
        const sortedIds = Array.from(folderEntries.keys()).sort((a,b) => a-b);

        let entriesOffset = folderEntries.size * 8;
        for(const id of sortedIds){
            const data = folderEntries.get(id);
            const tableOffset = 8 * (id & 0xFFF);

            // フォルダテーブルに書き込み
            const saved = w.length;
            w.a.length = tableOffset;
            while(w.a.length < tableOffset) w.a.push(0);

            w.putLInt(entriesOffset);
            w.putLShort(data.fileId);
            w.putLShort(data.parentId);

            const endPos = w.length;
            w.a.length = Math.max(saved, endPos);

            entriesOffset += data.entry.length;
        }

        // エントリテーブルを追加
        for(const id of sortedIds){
            w.putBytes(folderEntries.get(id).entry);
        }

        return w.toUint8();
    }
}

// ===================== 3. NARC クラス =====================
class Narc {
    constructor(){
        this.files = [];
        this.endian = 'little';
        this.fnt = new FntFolder('root');
    }

    // NARCファイルを読み込み
    static load(bytes){
        const narc = new Narc();
        const r = new BinReader(bytes);

        const magic = r.readString(4);
        if(magic !== 'NARC') throw new Error('Not a NARC file: ' + magic);

        const bom = r.getLShort();
        narc.endian = (bom === 0xFFFE) ? 'big' : 'little';

        r.skip(2); // version
        r.skip(4); // fileSize
        r.skip(2); // headerSize
        r.skip(2); // numBlocks

        // BTAF
        const fatbMagic = r.readString(4);
        if(fatbMagic !== 'BTAF') throw new Error('Invalid FATB');

        const fatbSize = r.getLInt();
        const numFiles = r.getLInt();

        const fileOffsets = [];
        for(let i = 0; i < numFiles; i++){
            fileOffsets.push({start: r.getLInt(), end: r.getLInt()});
        }

        // BTNF
        const fntbOffset = 0x10 + fatbSize;
        r.setPos(fntbOffset);
        const fntbMagic = r.readString(4);
        if(fntbMagic !== 'BTNF') throw new Error('Invalid FNTB');
        const fntbSize = r.getLInt();

        const fntData = r.slice(fntbSize - 8);
        narc.fnt = Fnt.load(fntData);

        // GMIF
        const fimgOffset = fntbOffset + fntbSize;
        r.setPos(fimgOffset);
        const fimgMagic = r.readString(4);
        if(fimgMagic !== 'GMIF') throw new Error('Invalid FIMG');
        r.skip(4); // size

        const rawDataOffset = r.getPos();

        for(const {start, end} of fileOffsets){
            r.setPos(rawDataOffset + start);
            narc.files.push(r.slice(end - start));
        }

        return narc;
    }

    // NARCファイルを保存
    save(keepFnt = true){
        const w = new BinWriter();

        // ファイルデータ
        const fimgData = new BinWriter();
        const fatbEntries = [];

        for(const file of this.files){
            const start = fimgData.length;
            fimgData.putBytes(file);
            const end = fimgData.length;
            fimgData.align(4);
            fatbEntries.push({start, end});
        }

        // BTAF
        const fatbBlock = new BinWriter();
        fatbBlock.putAscii('BTAF');
        fatbBlock.putLInt(0x0C + fatbEntries.length * 8);
        fatbBlock.putLInt(fatbEntries.length);
        for(const {start, end} of fatbEntries){
            fatbBlock.putLInt(start);
            fatbBlock.putLInt(end);
        }

        // BTNF
        const fntbBlock = new BinWriter();
        fntbBlock.putAscii('BTNF');
        const fntbSizePos = fntbBlock.length;
        fntbBlock.putLInt(0); // サイズは後で書き込む

        if(keepFnt && this.fnt && this.fnt.files.length > 0){
            const fntData = Fnt.save(this.fnt);
            fntbBlock.putBytes(fntData);
        } else {
            fntbBlock.putLInt(0x04000000);
            fntbBlock.putLShort(0);
            fntbBlock.putLShort(this.files.length);
        }
        fntbBlock.align(4, 0xFF);

        // アライメント後の正確なサイズを書き込む
        const fntbSize = fntbBlock.length;
        const savedData = fntbBlock.toUint8();
        const view = new DataView(savedData.buffer);
        view.setUint32(fntbSizePos, fntbSize, true);
        const fntbFinal = new BinWriter();
        fntbFinal.putBytes(savedData);

        // GMIF
        const fimgBlock = new BinWriter();
        fimgBlock.putAscii('GMIF');
        fimgBlock.putLInt(8 + fimgData.length);
        fimgBlock.putBytes(fimgData.toUint8());

        // ヘッダー
        const totalSize = 0x10 + fatbBlock.length + fntbFinal.length + fimgBlock.length;
        w.putAscii('NARC');
        w.putLShort(this.endian === 'big' ? 0xFEFF : 0xFFFE);
        w.putLShort(this.endian === 'big' ? 0x0001 : 0x0100);
        w.putLInt(totalSize);
        w.putLShort(0x10);
        w.putLShort(3);

        w.putBytes(fatbBlock.toUint8());
        w.putBytes(fntbFinal.toUint8());
        w.putBytes(fimgBlock.toUint8());

        return w.toUint8();
    }

    // API: ファイル取得 (名前 or ID)
    getFile(nameOrId){
        if(typeof nameOrId === 'string'){
            const id = this.fnt.getIdOf(nameOrId);
            return id >= 0 ? this.files[id] : null;
        }
        return this.files[nameOrId];
    }

    // API: ファイル設定 (名前 or ID)
    setFile(nameOrId, data){
        if(typeof nameOrId === 'string'){
            const id = this.fnt.getIdOf(nameOrId);
            if(id >= 0) this.files[id] = new Uint8Array(data);
        } else {
            this.files[nameOrId] = new Uint8Array(data);
        }
    }

    // API: ファイル追加
    addFile(data, name = null){
        this.files.push(new Uint8Array(data));
        if(name) this.fnt.addFile(name);
    }

    // API: ファイル削除
    removeFile(nameOrId){
        const id = typeof nameOrId === 'string' ? this.fnt.getIdOf(nameOrId) : nameOrId;
        if(id >= 0) this.files.splice(id, 1);
    }
}
