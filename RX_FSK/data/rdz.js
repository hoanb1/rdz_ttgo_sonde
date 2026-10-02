let stypes=new Map();
stypes.set('4', 'RS41 (Vaisala Weather Radiosonde)');
stypes.set('N', 'RS41-NFW (Amateur / Car Tracker)');
stypes.set('R', 'RS92 (Vaisala Legacy)');
stypes.set('D', 'DFM (Graw DFM-06/09/17)');
stypes.set('M', 'M10 (Meteomodem)');
stypes.set('2', 'M20 (Meteomodem)');
stypes.set('3', 'MP3H (Meteolabor)');


function loadaprs(baseurl,callback) {
  var link = document.createElement('link');
  link.rel = 'stylesheet';
  link.href = baseurl + "/aprs-symbols.css";
  document.head.appendChild(link);
  var script = document.createElement('script');
  script.src = baseurl + "/aprs-symbols.js";
  script.onload = function() { if (typeof callback === 'function') { callback(); } }
  document.head.appendChild(script);
}

document.addEventListener('DOMContentLoaded', function() {
 loadaprs("http://rdzsonde.mooo.com/aprs", function() {
  var inputBox = document.querySelector('input[name="tcp.beaconsym"]');
  if(inputBox) {
    inputBox.addEventListener('input', showaprs);
    var newdiv = document.createElement('div');
    newdiv.id = 'aprsSyms';
    inputBox.insertAdjacentElement('afterend', newdiv);
    function showaprs() {
      var inp = inputBox.value;
      var tag1 = getAPRSSymbolImageTag(inp.slice(0,2));
      var tag2 = getAPRSSymbolImageTag(inp.slice(2,4));
      if(typeof tag1 === 'string') { newdiv.innerHTML = "Fixed: "+tag1; }
      if(typeof tag2 === 'string') { newdiv.innerHTML += " Chase: "+tag2; }
    }
    showaprs();
    inputBox.addEventListener('input', function() { showaprs(); });
  }
 });
});
  
function footer() {
  document.addEventListener("DOMContentLoaded", function(){
    var form = document.querySelector(".wrapper");
    form.addEventListener("input", function() {
      document.querySelector(".save").disabled = false;
    });
    document.querySelector(".save").disabled = true;
  }); 
}

/* Used by qrg.html in RX_FSK.ino */
function prep() {
  var stlist=document.querySelectorAll("input.stype");
  for(txt of stlist){
    var val=txt.getAttribute('value'); var nam=txt.getAttribute('name'); 
    var sel=document.createElement('select');
    sel.setAttribute('name',nam);
    for(stype of stypes) { 
      var opt=document.createElement('option');
      opt.value=stype[0];
      opt.innerHTML=stype[1];
      if(stype[0]==val) { opt.setAttribute('selected','selected'); }
      sel.appendChild(opt);
    }
    txt.replaceWith(sel);
  }
} 

function qrgTable() {
  var tab=document.getElementById("divTable");

  var table = "<table><tr><th>Ch</th><th>Active</th><th>Frequency</th><th>Decoder</th><th>Launchsite</th></tr>";
  for(i=0; i<qrgs.length; i++) {
     var ck = "";
     if(qrgs[i][0]) ck="checked";
     table += "<tr><td class=\"ch\">" + (i+1) + "</td><td class=\"act\"><input name=\"A" + (i+1) + "\" type=\"checkbox\" " + ck + "/></td>";
     table += "<td><input name=\"F" + (i+1) + "\" type=\"text\" size=7 value=\"" + qrgs[i][1] + "\"></td>";
     table += "<td><input class=\"stype\" name=\"T" + (i+1) + "\" value=\"" + qrgs[i][3] + "\"></td>";
     table += "<td><input name=\"S" + (i+1) + "\" type=\"text\" value=\"" + qrgs[i][2] +"\"></td></tr>";
  }
  table += "</table>";
  tab.innerHTML = table;
  prep();
  footer();
}
