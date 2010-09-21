/*
 Copyright (c) 2010 Brian Silverman, Barry Silverman

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
*/

var statbox;

var nodes = new Array();
var transistors = {};

var ngnd = nodenames['vss'];
var npwr = nodenames['vcc'];


/////////////////////////
//
// Drawing Setup
//
/////////////////////////

// try to present a meaningful page before starting expensive work
function setup(){
	statbox = document.getElementById('status');
	setupNodes();
	setupTransistors();
	setupTable();
	initChip();
	document.getElementById('stop').style.visibility = 'hidden';
	go();
}

function setup_part2(){
}

function setupNodes(){
	var w = 0;
	for(var i in segdefs){
		var seg = segdefs[i];
		nodes[w] = {pullup: seg=='+',
		            state: 'fl', gates: new Array(), c1c2s: new Array()};
		w++;
		if(w==ngnd) continue;
		if(w==npwr) continue;
	}
}

function setupTransistors(){
	for(i in transdefs){
		var tdef = transdefs[i];
		var name = tdef[0];
		var gate = tdef[1];
		var c1 = tdef[2];
		var c2 = tdef[3];
		var trans = {name: name, on: false, gate: gate, c1: c1, c2: c2};
		nodes[gate].gates.push(name);
		nodes[c1].c1c2s.push(name);
		nodes[c2].c1c2s.push(name);
		transistors[name] = trans;
	}
}

/////////////////////////
//
// Etc.
//
/////////////////////////

function setStatus(){
	var res = '';
	for(var i=0;i<arguments.length;i++) res=res+arguments[i]+' ';
	statbox.innerHTML = res;
}

function now(){return  new Date().getTime();}
