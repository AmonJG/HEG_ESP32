const char event_page[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
<link rel="stylesheet" type="text/css" href="webDemoCSS.css">
<script src="HEGwebAPI.js"></script>
</head>
<body>
  <title>HEG Interface</title>
  <div class="header">
      <h1>HEG ALPHA Ver 0.0.3</h1>
  </div>
  
  <div id="main_body"></div>

  
<script> //Rough Draft scripts. Generalize the overlays, onData, replayCSV, and handleEventData calls
  
  //Advanced Client scripts using external packages
  //Detect that we are not using the default local hosting on the ESP32 so we can grab scripts
  if(window.location.hostname != "192.168.4.1" || "esp32.local") {
    var useAdvanced = true; //Create a global flag to indicate we're using advanced scripts.
  }
  
  if(useAdvanced) { //Setup advanced scripts
    var link = document.createElement("script");
    link.src = "https://cdnjs.cloudflare.com/ajax/libs/three.js/110/three.min.js"; // Can set this to be a nonlocal link like from cloudflare or a special script with a custom app
    document.head.appendChild(link); //Append script
  
    //Initialize whatever.
    function createThreeApp() {
      var rendererHTML = '<div id="threeContainer" class="canvasContainer"></div>';
      HEGwebAPI.appendFragment(rendererHTML,'main_body');
      
      window.scene = new THREE.Scene();
      window.camera = new THREE.PerspectiveCamera( 75, (window.innerWidth - 20) / 430, 0.1, 1000 );

      window.renderer = new THREE.WebGLRenderer();
      renderer.setSize(window.innerWidth, 430);

      document.getElementById("threeContainer").appendChild(renderer.domElement);
    
      //material
      var material = new THREE.MeshNormalMaterial( {
        wireframe: true
      } );

      //sphere
      var sphere = new THREE.SphereGeometry(2,20,20);
      var sphereMesh = new THREE.Mesh( sphere, material );
      scene.add( sphereMesh );

      camera.position.x = 0;
      camera.position.y = 0;
      camera.position.z = 5;

      window.threeAnim;
      window.threeWidth = window.innerWidth - 20;

      function render() {
        
        if(window.threeWidth != window.innerWidth - 20) {
          window.threeWidth = window.innerWidth - 20;
          renderer.setSize(window.threeWidth, 430);
          camera.aspect = window.threeWidth / 430;
          camera.updateProjectionMatrix();
        }
        threeAnim = requestAnimationFrame(render);

        renderer.render(scene, camera);
      }
      render();
    }

    function destroyThreeApp() {
      cancelAnimationFrame(threeAnim);
      renderer.domElement.addEventListener('dblclick', null, false); //remove listener to render
      scene = null;
      projector = null;
      camera = null;
      controls = null;
      document.getElementById("threeContainer").remove();
    }
  }

//Session
var s = new HEGwebAPI('',false);

//Graph
var g = new graphJS(1000,[255,100,80,1],1,[1400,600], "main_body", "g", false); //This could be swapped for a superior graphing package

//Feedback
var c = new circleJS(); //Default animation initialized first
var v = null;
var a = null;
var h = null;

if(useAdvanced) {
  var useThree = false;
}

//------------------------------------------------------------------------
//------------------------------------------------------------------------
//------------------------------------------------------------------------

// Modal Code
var switchHTML = '<label class="switch"><input type="checkbox" id="togBtn"><div class="startslider round"></div></label>';

var tabHTML = '<div id="tabContainer"> \
  <button class="tablink" id="modal_opener">Data</button> \
  <button class="tablink" id="modal_opener2">Graph</button> \
  <button class="tablink" id="modal_opener3">Feedback</button> \
  </div> \
  <div id="modal" class="modal" style="display: none"> \
  <div id="overlay" class="overlay"></div> \
    <div class="modal_content databoxmodal"> \
      <h2>Data Options</h2> \
      <div id="dataBox"></div> \
      <button title="Close" id="close_modal" class="close_modal"> \
          <i class="fas fa-times"></i> \
      </button> \
    </div> \
  </div> \
  <div id="modal2" class="modal" style="display: none"> \
  <div id="overlay2" class="overlay"></div> \
    <div id="graphBox" class="modal_content graphboxmodal"> \
      <h2>Graph Options</h2> \
      <button title="Close" id="close_modal2" class="close_modal"> \
          <i class="fas fa-times"></i> \
      </button> \
    </div> \
  </div> \
  <div id="modal3" class="modal" style="display: none"> \
  <div id="overlay3" class="overlay"></div> \
    <div id = "visualBox" class="modal_content feedbackboxmodal"> \
      <h2>Feedback Options</h2> \
      <button title="Close" id="close_modal3" class="close_modal"> \
          <i class="fas fa-times"></i> \
      </button> \
    </div> \
  </div>';


HEGwebAPI.appendFragment(switchHTML, "main_body");
HEGwebAPI.appendFragment(tabHTML, "main_body");

function attachModalListeners(modalElm, closemodal, overlay) {
  document.getElementById(closemodal).onclick = function() {toggleModal(modalElm, closemodal, overlay)};
  document.getElementById(overlay).onclick = function() {toggleModal(modalElm, closemodal, overlay)};
}

function detachModalListeners(modalElm, closemodal, overlay) {
  document.getElementById(closemodal).onclick = function() {toggleModal(modalElm, closemodal, overlay)};
  document.getElementById(overlay).onclick = function() {toggleModal(modalelm, closemodal, overlay)};
}

function toggleModal(modalElm, closemodal, overlay) {
  var currentState = modalElm.style.display;
  // If modal is visible, hide it. Else, display it.
  if (currentState === 'none') {
    modalElm.style.display = 'block';
    attachModalListeners(modalElm, closemodal, overlay);
  } else {
    modalElm.style.display = 'none';  
    detachModalListeners(modalElm, closemodal, overlay);
  }
}

var modal = document.getElementById('modal');
var modal2 = document.getElementById('modal2');
var modal3 = document.getElementById('modal3');

document.getElementById('modal_opener').onclick = function() {toggleModal(modal,'close_modal','overlay')};
document.getElementById('modal_opener2').onclick = function() {toggleModal(modal2,'close_modal2','overlay2')};
document.getElementById('modal_opener3').onclick = function() {toggleModal(modal3,'close_modal3','overlay3')};

function toggleHEG(switchElement) {
  if (switchElement.checked) {
    document.getElementById('startbutton').click();
  } else {
    document.getElementById('stopbutton').click();
  }
}
document.getElementById("togBtn").onchange = function(){toggleHEG(document.getElementById("togBtn"))};

//------------------------------------------------------------------------
//------------------------------------------------------------------------
//------------------------------------------------------------------------



s.createUI("dataBox");
g.createUI("graphBox")

var modeHTML = '<div class="menudiv" id="menudiv"> \
  Modes:<br> \
  <button class="button" id="canvasmode">Circle</button> \
  <button class="button" id="videomode">Video</button> \
  <button class="button" id="audiomode">Audio</button><br> \
  <button class="button" id="hillmode">Hill Climb</button>';

if(useAdvanced) {
  modeHTML += '<button class="button" id="threemode">ThreeJS</button>';
}

modeHTML += '</div>';

HEGwebAPI.appendFragment(modeHTML,"visualBox");

function deInitMode(){
  if(v != null){
    var thisNode = document.getElementById(v.vidapiId);
    thisNode.parentNode.parentNode.removeChild(thisNode.parentNode);
    thisNode = document.getElementById(v.vidContainerId);    
    thisNode.parentNode.parentNode.removeChild(thisNode.parentNode);
    v.deInit();
    v = null;
  }
  if(a != null){
    a.stopAudio();
    a.endAudio(a);
    var thisNode = document.getElementById(a.audioId);
    thisNode.parentNode.parentNode.removeChild(thisNode.parentNode);
    thisNode = document.getElementById(a.audmenuId);
    thisNode.parentNode.parentNode.removeChild(thisNode.parentNode);
    a = null;
  }
  if(c != null){
    c.deInit();
    c.c.parentNode.parentNode.parentNode.removeChild(c.c.parentNode.parentNode);
    c = null;
  }
  if(h != null){
    h.deInit();
    h.c.parentNode.parentNode.parentNode.removeChild(h.c.parentNode.parentNode);
    h.menu.parentNode.removeChild(h.menu);
    h = null;
  }
  if(useAdvanced) {
    if(useThree == true) {
      useThree = false;
      destroyThreeApp();
    }
  }
}

document.getElementById("canvasmode").onclick = function() {
  if(c == null){
    deInitMode();
    c = new circleJS();
  }
}

document.getElementById("videomode").onclick = function() {
  if(v == null){
    deInitMode();
    v = new videoJS();
  }
}

document.getElementById("audiomode").onclick = function() {
  if(a == null){
    deInitMode();
    a = new audioJS();
  }
}

document.getElementById("hillmode").onclick = function() {
  if(h == null){
    deInitMode();
    h = new hillJS();
  }
}

if(useAdvanced) {
  document.getElementById("threemode").onclick = function() {
    if(useThree == false) {
      deInitMode();
      createThreeApp();
      useThree = true;
    }
  }
}

g.xoffsetSlider.onchange = () => {
   if(g.xoffsetSlider.value > s.scoreArr.length) {
     g.xoffsetSlider.value = s.scoreArr.length - 1;
   }
   g.xoffset = g.xoffsetSlider.value;
   
   if(s.scoreArr.length > g.graphY1.length){ //more data than graph size, so just grab a slice of the graph
    var endIndex = s.scoreArr.length - g.xoffset - 1;
    g.graphY1 = s.scoreArr.slice(endIndex - g.graphY1.length, endIndex); // FIX 
    g.graphY2 = s.ratio.slice(endIndex -g.graphY2.length, endIndex);
   }
   else if (s.scoreArr.length < g.graphY1.length) { //less data than graph size, generate zeroes with data from 0 to offset
    var scoreslice = s.scoreArr.slice(0,s.scoreArr.length - 1 - g.xoffset);
    var ratioslice = s.ratio.slice(0,s.ratio.length - 1 - g.xoffset);
    if(g.graphY1.length == scoreslice){
      g.graphY1 = scoreslice;
      g.graphY2 = ratioslice;
    }
    else{
      g.graphY1 = [...Array(g.VERTEX_LENGTH - scoreslice.length).fill(0), ...scoreslice];
      g.graphY2 = [...Array(g.VERTEX_LENGTH - ratioslice.length).fill(0), ...ratioslice];
    }
   }
}

g.xscaleSlider.onchange = () => {
  len = g.graphY1.length;
  if(g.xscaleSlider.value < len) { // Remove from front.
    for(var i = 0; i < len - g.xscaleSlider.value; i++){
      g.graphY1.shift();
      g.graphY2.shift();
    }
  }
  if(g.xscaleSlider.value > len) { // Add to front
    for(var i = 0; i < g.xscaleSlider.value - len; i++){
      if(i+len+g.xoffset <= s.scoreArr.length){
        g.graphY1.unshift(s.scoreArr[s.scoreArr.length - ((len+i) + g.xoffset)]);
        g.graphY2.unshift(s.ratio[s.ratio.length - ((len+i) + g.xoffset)]);
      } 
      else{
        g.graphY1.unshift(0);
        g.graphY2.unshift(0);
      }
    }
  }
  g.VERTEX_LENGTH = g.graphY1.length;
}

document.getElementById("xscalebutton").onclick = () => {
  var len = g.graphY1.length;
  g.xscaleSlider.value = 1000;
  if(g.xscaleSlider.value < len) { // Remove from front.
    for(var i = 0; i < len - g.xscaleSlider.value; i++){
      g.graphY1.shift();
      g.graphY2.shift();
    }
  }
  if(g.xscaleSlider.value > len) { // Add to front
    for(var i = 0; i < g.xscaleSlider.value - len; i++){
      if(g.xscaleSlider.value < s.scoreArr.length){
        g.graphY1.unshift(s.scoreArr[s.scoreArr.length - 1 - ((g.graphY1.length+i) + g.xoffset)]);
        g.graphY2.unshift(s.ratio[s.ratio.length - 1 - ((g.graphY2.length+i) + g.xoffset)]);
      } 
      else{
        g.graphY1.unshift(0);
        g.graphY2.unshift(0);
      }
    }
  }
  g.VERTEX_LENGTH = g.xscaleSlider.value;
}

//document.getElementById("resetButton").onclick = () => {
  //s.resetVars();
  //g.graphY1 = [...Array(g.VERTEX_LENGTH).fill(0)]
//}

//Customize session functions
s.handleScore = function() {
  parent.postMessage( this.ratio[this.ratio.length-1], "*");
  g.us = this.us[this.us.length - 1] - this.startTime;
  if(this.ratio.length > 40){
    this.smaScore(this.ratio);
    var score = this.smaSlope*this.sensitivity.value*0.01;
    if(c != null){
      c.onData(score);
    }
    if (v != null) {
      v.onData(score);
    }
    if(a != null) {
      a.onData(score);
    }
    if(h != null) {
      h.onData(score);
    }
    if(useAdvanced) {
      if(useThree == true) {

      }
    }
    this.scoreArr.push(this.scoreArr[this.scoreArr.length - 1] + score);
    g.ratio = this.slowSMA;
    g.score = this.scoreArr[this.scoreArr.length - 1];
    g.graphY1.shift();
    g.graphY1.push(this.scoreArr[this.scoreArr.length - 1 - g.xoffset]);
    g.graphY2.shift();
    g.graphY2.push(this.slowSMAarr[this.slowSMAarr.length - 1 - g.xoffset]);
  }
  else {
    //this.smaSlope = this.scoreArr[this.scoreArr.length - 1];
    //g.graphY1.shift();
    //g.graphY1.push(this.smaSlope);
    //this.scoreArr.push(this.smaSlope);
  }
  this.updateTable();
}

s.endOfEvent = function() {
  if(g.xoffsetSlider.max < this.scoreArr.length){
    if(this.scoreArr.length % 20 == 0) { 
      g.xoffsetSlider.max = this.scoreArr.length - 3; //Need 2 vertices minimum
    }
  }
}

</script>
</body>
</html>
)=====";
