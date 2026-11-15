/* Offline-only fallback for this delivery environment. Production builds on
   Windows/macOS continue to use `npm run build` (Vite). This tool bundles
   locally installed CJS/TSX modules with no fetching or native Rollup binary. */
const fs = require('fs');
const path = require('path');
const ts = require('typescript');
const { createRequire } = require('module');
const root = path.resolve(__dirname, '..');
const dest = path.resolve(root, '../ui/app');
const entries = [];
const keys = new Map();
const styles = [];
const regex = /\brequire\(\s*['"]([^'"]+)['"]\s*\)/g;
const isCode = p => /\.(?:js|jsx|cjs|mjs|ts|tsx)$/.test(p);
function resolve(from,spec) {
  if(spec.startsWith('.')){
    const base=path.resolve(path.dirname(from),spec);
    for(const ext of ['','.ts','.tsx','.js','.jsx','.cjs','.mjs','.json','/index.ts','/index.tsx','/index.js']){
      const file=base+ext;
      if(fs.existsSync(file)&&fs.statSync(file).isFile())return fs.realpathSync(file);
    }
  }
  const req=createRequire(from);
  return fs.realpathSync(req.resolve(spec));
}
function add(filename) {
  filename = fs.realpathSync(filename);
  if (keys.has(filename)) return keys.get(filename);
  const id = entries.length;
  keys.set(filename,id);
  entries.push(null);
  const ext=path.extname(filename);
  let code = fs.readFileSync(filename,'utf8');
  if(ext==='.json')code=`module.exports = ${code.trim()};`;
  else if(ext==='.ts'||ext==='.tsx'||ext==='.mjs'||ext==='.jsx'){
    code=ts.transpileModule(code,{fileName:filename,compilerOptions:{
      module:ts.ModuleKind.CommonJS,jsx:ts.JsxEmit.ReactJSX,
      target:ts.ScriptTarget.ES2020,esModuleInterop:true,allowJs:true,
    }}).outputText;
  }
  const map = {};
  for (const m of code.matchAll(regex)) {
    const spec = m[1];
    if(Object.hasOwn(map,spec))continue;
    if(spec.endsWith('.css')){map[spec]=-1;continue;}
    let next;
    try{next=resolve(filename,spec)}catch(err){
      throw new Error(`Unresolvable dependency ${spec} in ${filename}: ${err.message}`);
    }
    if(!isCode(next)&&!next.endsWith('.json'))throw new Error(`Unsupported dependency ${next}`);
    map[spec]=add(next);
  }
  entries[id]={id,file:path.relative(root,filename),code,map};
  return id;
}
const entry=add(path.join(root,'src/main.tsx'));
if(entry!==0)throw new Error('Unexpected entry');
const modules=entries.map(({id,code,map,file})=>
`${id}: [function(module, exports, require) {\n${code}\n}, ${JSON.stringify(map)}, ${JSON.stringify(file)}]`).join(',\n');
const bundle=`/* Live Stage | offline-compatible bundled React production GUI */\n(function(){\n'use strict';\nconst process={env:{NODE_ENV:'production'}};\nconst modules={${modules}};\nconst cache={};\nfunction load(id){\n if(cache[id]) return cache[id].exports;\n const record=modules[id]; if(!record) throw new Error('Module '+id+' not found');\n const mod={exports:{}};cache[id]=mod;\n const [fn,bindings,file]=record;\n const localRequire=(spec)=>{if(!Object.hasOwn(bindings,spec)) throw new Error('Unknown import '+spec+' from '+file); if(bindings[spec]<0)return {};return load(bindings[spec]);};\n fn(mod,mod.exports,localRequire);return mod.exports;\n}\nload(${entry});\n})();`;
fs.mkdirSync(path.join(dest,'assets'),{recursive:true});
fs.writeFileSync(path.join(dest,'assets/app.js'),bundle);
for(const file of ['theme.css','components.css','views.css','pro-console.css','perform-polish.css','nord-buttons.css']){
  styles.push('/* '+file+' */\n'+fs.readFileSync(path.join(root,'src/styles',file),'utf8'));
}
fs.writeFileSync(path.join(dest,'assets/app.css'),styles.join('\n'));
const html=fs.readFileSync(path.join(root,'index.html'),'utf8')
 .replace('</head>','  <link rel="stylesheet" href="/app/assets/app.css" />\n  </head>')
 .replace('<script type="module" src="/src/main.tsx"></script>', '<script defer src="/app/assets/app.js"></script>');
fs.writeFileSync(path.join(dest,'index.html'),html);
console.log('Bundled',entries.length,'modules;', (bundle.length/1048576).toFixed(2),'MiB JS; CSS',Math.round(styles.join('').length/1024),'KiB');
