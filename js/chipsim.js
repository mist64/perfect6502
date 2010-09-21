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

var ctrace = true;
var loglevel = 3;

function recalcNodeList(list){
	var n = list[0];
	var recalclist = new Array();
	for(var j=0;j<100;j++){		// loop limiter
		if(list.length==0) return;
		if(ctrace) console.log(j, list);
		for(var i in list) recalcNode(list[i], recalclist);
		list = recalclist;
		recalclist = new Array();
	}
	if(ctrace) console.log(n,'looping...');
}

function recalcNode(node, recalclist){
	if(node==ngnd) return;
	if(node==npwr) return;
	var group = getNodeGroup(node);
	var newv = getNodeValue(group);
	if(ctrace) console.log('recalc', node, group);
	for(var i in group){
		var n = nodes[group[i]];
		if(n.state!=newv && ctrace) console.log(group[i], n.state, newv);
		n.state = newv;
		for(var t in n.gates) recalcTransistor(n.gates[t], recalclist);
	}
}

function recalcTransistor(tn, recalclist){
	var t = transistors[tn];
	if(isNodeHigh(t.gate)) turnTransistorOn(t, recalclist);
	else turnTransistorOff(t, recalclist);
}

function turnTransistorOn(t, recalclist){
	if(t.on) return;
	if(ctrace) console.log(t.name, 'on', t.gate, t.c1, t.c2);
	t.on = true;
	addRecalcNode(t.c1, recalclist);
	addRecalcNode(t.c2, recalclist);
}

function turnTransistorOff(t, recalclist){
	if(!t.on) return;
	if(ctrace) console.log(t.name, 'off', t.gate, t.c1, t.c2);
	t.on = false;
	floatnode(t.c1);
	floatnode(t.c2);
	addRecalcNode(t.c1, recalclist);
	addRecalcNode(t.c2, recalclist);
}

function floatnode(nn){
	if(nn==ngnd) return;
	if(nn==npwr) return;
	var n = nodes[nn];
	if(n.state=='gnd') n.state = 'fl';
	if(n.state=='pd') n.state = 'fl';
	if(n.state=='vcc') n.state = 'fh';
	if(n.state=='pu') n.state = 'fh';
	if(ctrace) console.log('floating', nn, 'to', n.state);
}

function addRecalcNode(nn, recalclist){
	if(nn==ngnd) return;
	if(nn==npwr) return;
	if(arrayContains(recalclist, nn)) return;
	recalclist.push(nn);
}

function getNodeGroup(i){
	var group = new Array();
	addNodeToGroup(i, group);
	return group;
}

function addNodeToGroup(i, group){
	if(arrayContains(group, i)) return;
	group.push(i);
	if(i==ngnd) return;
	if(i==npwr) return;
	for(var t in nodes[i].c1c2s) addNodeTransistor(i, nodes[i].c1c2s[t], group);
}

function addNodeTransistor(node, t, group){
	var tr = transistors[t];
	if(!tr.on) return;
	var other;
	if(tr.c1==node) other=tr.c2;
	if(tr.c2==node) other=tr.c1;
	addNodeToGroup(other, group);
}


function getNodeValue(group){
	if(arrayContains(group, ngnd)) return 'gnd';
	if(arrayContains(group, npwr)) return 'vcc';
	var flstate;
	for(var i in group){
		var nn = group[i];
		var n = nodes[nn];
		if(n.pullup) return 'pu';
		if(n.pulldown) return 'pd';
		if((n.state=='fl')&&(flstate==undefined)) flstate = 'fl';
		if(n.state=='fh') flstate = 'fh';
	}
	if(flstate==undefined && ctrace) console.log(group);
	return flstate;
}


function isNodeHigh(nn){
	return arrayContains(['vcc','pu','fh'], nodes[nn].state);
}

function allNodes(){
	var res = new Array();
	for(var i in nodes) if((i!=npwr)&&(i!=ngnd)) res.push(i);
	return res;
}

function setHigh(name){
	var nn = nodenames[name];
	nodes[nn].pullup = true;
	nodes[nn].pulldown = false;
	recalcNodeList([nn]);
}

function setLow(name){
	var nn = nodenames[name];
	nodes[nn].pullup = false;
	nodes[nn].pulldown = true;
	recalcNodeList([nn]);
}

function arrayContains(arr, el){return arr.indexOf(el)!=-1;}
