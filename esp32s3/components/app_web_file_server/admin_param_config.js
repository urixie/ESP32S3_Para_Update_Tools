function switchParamType(type){
  const state=getCurrentBoardState();
  if(state)state.currentParamType=type;
  currentParamType=type;
  renderParamTable();
}
function renderParamTable(){
  const state=getCurrentBoardState();
  const params=getCurrentParams();
  const activeType=(state&&state.currentParamType)||currentParamType||'control';
  currentParamType=activeType;
  parsedParams=params;
  const control=params.filter(p=>normParamType(p)==='control');
  const protection=params.filter(p=>normParamType(p)==='protection');
  $('controlCount').textContent=control.length;
  $('protectionCount').textContent=protection.length;
  $('tabControl').classList.toggle('active',activeType==='control');
  $('tabProtection').classList.toggle('active',activeType==='protection');

  const list=params
    .map((p,i)=>({p,i}))
    .filter(x=>normParamType(x.p)===activeType);

  const rows=[];
  for(let row=0;row<Math.ceil(list.length/2);row++){
    const left=list[row];
    const right=list[row+Math.ceil(list.length/2)];
    const cells=[left,right].map(x=>{
      if(!x)return '<td class="name param-empty-cell"></td><td class="value param-empty-cell"></td>';
      const v=x.p.currentValue==null?'':x.p.currentValue;
      return '<td class="name">'+esc(x.p.name)+'</td>'
        +'<td class="value"><span class="param-value-cell"><input class="param-value-input" type="number" min="0" max="'+PARAM_MAX_NS+'" step="'+PARAM_TIME_BASE_NS+'" inputmode="numeric" data-index="'+x.i+'" value="'+esc(v)+'"></span></td>';
    }).join('');
    rows.push('<tr>'+cells+'</tr>');
  }
  $('paramRows').innerHTML=rows.join('');

  document.querySelectorAll('.param-value-input').forEach(input=>{
    input.oninput=()=>{
      const idx=Number(input.dataset.index);
      if(!params[idx])return;

      if(input.value===''){
        delete params[idx].currentValue;
        return;
      }

      let v=Number(input.value);
      if(!Number.isFinite(v)){
        delete params[idx].currentValue;
        input.value='';
        return;
      }

      if(v<0)v=0;
      if(v>PARAM_MAX_NS)v=PARAM_MAX_NS;
      v=Math.floor(v);

      params[idx].currentValue=v;
      input.value=String(v);
    };
  });

  $('paramEmpty').classList.toggle('hidden',list.length>0);
  $('paramTable').classList.toggle('hidden',list.length===0);
}
async function parseSelected(){
  if(!selected)return;
  const path=selected.path;
  $('parseErr').classList.add('hidden');$('result').classList.add('hidden');
  $('title').textContent=boardLabel(selected);
  $('hint').textContent='系统正在解析板卡配置...';
  if(!paramBusy)setParamBusy(false);
  try{
    const r=await fetch('/api/bin/parse?path='+encodeURIComponent(path));
    const j=await r.json();
    if(r.status===401){location.href='/';return;}
    if(!r.ok||!j.ok)throw new Error(j.error||r.statusText);
    const file=binFiles.find(f=>f.path===path)||allFiles.find(f=>f.path===path);
    if(j.boardName&&file){file.boardName=j.boardName;file.displayName=j.boardName;}
    const state=boardParamStates[path]||{parameters:[],currentParamType:'control',parsed:false};
    const oldParams=state.parameters||[];
    const newParams=(j.parameters||[]).map((p,i)=>{
      const oldByAddr=oldParams.find(o=>o&&o.address!=null&&p.address!=null&&Number(o.address)===Number(p.address));
      const oldByKey=oldParams.find(o=>o&&o.name===p.name&&normParamType(o)===normParamType(p));
      const old=oldByAddr||oldByKey||oldParams[i];
      return old&&old.currentValue!=null?{...p,currentValue:old.currentValue}:{...p,currentValue:p.defaultValue};
    });
    state.parameters=newParams;
    state.currentParamType='control';
    state.parsed=true;
    boardParamStates[path]=state;
    paramCounts[path]=(typeof j.visibleCount==='number')?j.visibleCount:newParams.length;
    if(selected&&selected.path===path&&j.boardName){selected.boardName=j.boardName;selected.displayName=j.boardName;$('title').textContent=j.boardName;}
    renderFiles();
    renderBins();
    if(selected&&selected.path===path){
      currentParamType=state.currentParamType;
      renderParamTable();
      $('result').classList.remove('hidden');
      $('parseErr').classList.add('hidden');
      $('hint').textContent='';
    }
  }catch(e){
    if(selected&&selected.path===path){
      $('parseErr').textContent='解析失败：'+e.message;
      $('parseErr').classList.remove('hidden');
      $('hint').textContent='解析失败，请确认该文件由配套工具生成且未损坏';
    }
  }
}
async function deleteFileByPath(path){
  const f=binFiles.find(x=>x.path===path);
  const label=f?boardLabel(f):'该板卡配置';
  if(!confirm('确认删除板卡配置 '+label+' 吗？'))return;
  setMsg('正在删除板卡配置...');
  const body='path='+encodeURIComponent(path);
  const r=await fetch('/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  if(r.status===401){location.href='/';return;}
  if(!r.ok){setMsg('删除失败：'+await readErr(r));return;}
  delete boardParamStates[path];
  delete paramCounts[path];
  if(selected&&selected.path===path){selected=null;parsedParams=[];$('result').classList.add('hidden');$('parseErr').classList.add('hidden');$('title').textContent='请选择一个板卡';}
  setMsg('删除完成',true);loadFiles();
}
function actDefault(){
  const state=getCurrentBoardState();
  if(!selected||!state||state.parameters.length===0)return;
  state.parameters.forEach(p=>{p.currentValue=p.defaultValue;});
  renderParamTable();
  showToast('默认参数填充成功');
}
async function actReadback(){
  const state=getCurrentBoardState();
  if(!selected||!state)return;
  const path=selected.path;
  try{
    setParamBusy(true);
    showToast('正在回读参数...',1200);
    const j=await runParamBoardOperation('正在连接板卡并回读参数','/api/param/readback',{path});
    if(!j)return;
    if(!(selected&&selected.path===path))return;
    const valueMap={};
    (j.values||[]).forEach(item=>{valueMap[Number(item.address)]=item.value;});
    state.parameters.forEach(p=>{
      const addr=Number(p.address);
      if(Object.prototype.hasOwnProperty.call(valueMap,addr)){
        p.currentValue=valueMap[addr];
      }
    });
    renderParamTable();
    showToast('参数回读成功');
  }catch(e){
    showToast('参数回读失败：'+e.message,1800);
  }finally{
    setParamBusy(false);
  }
}
function formatParamValue(v){
  if(v==null||v==='')return '未回读';
  const n=Number(v);
  if(Number.isFinite(n))return Math.floor(n)+' ns';
  return String(v);
}
function collectDownloadPayload(params){
  const pairs=[];
  const items=[];
  for(const p of params){
    const addr=Number(p.address);
    if(!Number.isInteger(addr)||addr<0||addr>71){
      showToast('参数地址无效',1800);
      return null;
    }
    if(p.currentValue==null||p.currentValue===''){
      showToast('请先填写所有可见参数',1800);
      return null;
    }
    let v=Number(p.currentValue);
    if(!Number.isFinite(v)||v<0||v>PARAM_MAX_NS){
      showToast('参数值必须在 0~'+PARAM_MAX_NS+' ns',1800);
      return null;
    }
    v=Math.floor(v);
    if(v%PARAM_TIME_BASE_NS!==0){
      showToast('参数值必须是 '+PARAM_TIME_BASE_NS+'ns 的整数倍',1800);
      return null;
    }
    pairs.push(addr+':'+v);
    items.push({address:addr,name:p.name||'未命名参数',value:v});
  }
  return {pairs,items};
}
function buildDownloadDiffs(readbackValues,downloadItems){
  const valueMap={};
  (readbackValues||[]).forEach(item=>{
    const addr=Number(item.address);
    if(Number.isInteger(addr))valueMap[addr]=item.value;
  });
  return downloadItems.reduce((list,item)=>{
    const hasReadback=Object.prototype.hasOwnProperty.call(valueMap,item.address);
    const readbackValue=hasReadback?valueMap[item.address]:null;
    const readbackNumber=Number(readbackValue);
    if(!hasReadback||!Number.isFinite(readbackNumber)||Math.floor(readbackNumber)!==item.value){
      list.push({name:item.name,readbackValue,inputValue:item.value});
    }
    return list;
  },[]);
}
function renderDownloadDiffDialog(diffs){
  const hasDiff=diffs.length>0;
  $('downloadDiffCount').textContent=hasDiff?'共 '+diffs.length+' 项差异':'0 项差异';
  $('downloadDiffWrap').classList.toggle('hidden',!hasDiff);
  $('downloadDiffEmpty').classList.toggle('hidden',hasDiff);
  $('downloadDiffRows').innerHTML=diffs.map(d=>
    '<div class="download-diff-row">'
      +'<div class="download-diff-cell readback">'
        +'<div class="download-diff-name">'+esc(d.name)+'</div>'
        +'<div class="download-diff-value">'+esc(formatParamValue(d.readbackValue))+'</div>'
      +'</div>'
      +'<div class="download-diff-cell input">'
        +'<div class="download-diff-name">'+esc(d.name)+'</div>'
        +'<div class="download-diff-value">'+esc(formatParamValue(d.inputValue))+'</div>'
      +'</div>'
    +'</div>'
  ).join('');
}
function openDownloadConfirmDialog(diffs){
  renderDownloadDiffDialog(diffs);
  $('downloadConfirmDialog').classList.remove('hidden');
  return new Promise(resolve=>{downloadConfirmResolver=resolve;});
}
function closeDownloadConfirmDialog(){
  $('downloadConfirmDialog').classList.add('hidden');
}
function resolveDownloadConfirm(confirmed){
  const resolve=downloadConfirmResolver;
  downloadConfirmResolver=null;
  closeDownloadConfirmDialog();
  if(resolve)resolve(confirmed);
}
function cancelDownloadConfirm(){
  resolveDownloadConfirm(false);
}
function confirmDownloadConfirm(){
  resolveDownloadConfirm(true);
}
async function actDownload(){
  const state=getCurrentBoardState();
  if(!selected||!state)return;
  const path=selected.path;
  const payload=collectDownloadPayload(state.parameters);
  if(!payload)return;
  try{
    setParamBusy(true);
    showToast('正在回读参数...',1200);
    const readback=await runParamBoardOperation('正在连接板卡并回读参数','/api/param/readback',{path});
    if(!readback)return;
    if(!(selected&&selected.path===path))return;
    const diffs=buildDownloadDiffs(readback.values,payload.items);
    const confirmed=await openDownloadConfirmDialog(diffs);
    if(!confirmed){
      showToast('已取消下载',1200);
      return;
    }
    showToast('正在下载参数...',1200);
    const j=await runParamBoardOperation('正在连接板卡并下载参数','/api/param/download',{path,values:payload.pairs.join(',')});
    if(!j)return;
    showToast('参数下载成功');
  }catch(e){
    showToast('参数下载失败：'+e.message,1800);
  }finally{
    setParamBusy(false);
  }
}
