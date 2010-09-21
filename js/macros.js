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

var memory = Array();
var code = [0xa9, 0x00, 0x20, 0x10, 0x00, 0x4c, 0x02, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0xe8, 0x88, 0xe6, 0x40, 0x38, 0x69, 0x02, 0x60];
var cycle = 0;
var running = false;

function go(n){
	for(var i=0;i<code.length;i++){
		mWrite(i, code[i]);
		setCellValue(i, code[i]);
	}
	mWrite(0xfffc, 0x00);
	mWrite(0xfffd, 0x00);
	steps();
}

function steps(){
	if(running) {
           step();
	   setTimeout(steps, 0); // schedule the next poll
        }
}

function initChip(){
	for(var nn in nodes) nodes[nn].state = 'fl';
	nodes[ngnd].state = 'gnd';
	nodes[npwr].state = 'vcc';
	for(var tn in transistors) transistors[tn].on = false;
	setLow('res');
	setLow('clk0');
	setHigh('rdy'); setLow('so');
	setHigh('irq'); setHigh('nmi');
	recalcNodeList(allNodes()); 
	for(var i=0;i<8;i++){setHigh('clk0'), setLow('clk0');}
	setHigh('res');
	for(var i=0;i<18;i++){resetStep();}
	cycle = 0;
	chipStatus();
}

function step(){
	halfStep();
	cycle++;
	chipStatus();
}

function halfStep(){
	var clk = isNodeHigh(nodenames['clk0']);
	if (clk) {setLow('clk0'); handleBusRead(); } 
	else {setHigh('clk0'); handleBusWrite();}
}

function resetStep(){
	var clk = isNodeHigh(nodenames['clk0']);
	if (clk) {setLow('clk0'); handleBusRead(); }
	else {setHigh('clk0'); handleBusWrite();}
}

function handleBusRead(){
	if(isNodeHigh(nodenames['rw'])) writeDataBus(mRead(readAddressBus()));
}

function handleBusWrite(){
	if(!isNodeHigh(nodenames['rw'])){
		var a = readAddressBus();
		var d = readDataBus();
		mWrite(a,d);
		if(a<0x200) setCellValue(a,d);
	}
}

function readAddressBus(){return readBits('ab', 16);}
function readDataBus(){return readBits('db', 8);}
function readA(){return readBits('a', 8);}
function readY(){return readBits('y', 8);}
function readX(){return readBits('x', 8);}
function readP(){return readBits('p', 8);}
function readPstring(){
   var result;
   result = (isNodeHigh(nodenames['p7'])?'N':'n') +
            (isNodeHigh(nodenames['p6'])?'V':'v') +
            '-' +
            (isNodeHigh(nodenames['p3'])?'B':'b') +
            (isNodeHigh(nodenames['p3'])?'D':'d') +
            (isNodeHigh(nodenames['p2'])?'I':'i') +
            (isNodeHigh(nodenames['p1'])?'Z':'z') +
            (isNodeHigh(nodenames['p0'])?'C':'c');
   return result;
}
function readSP(){return readBits('s', 8);}
function readPC(){return (readBits('pch', 8)<<8) + readBits('pcl', 8);}
function readPCL(){return readBits('pcl', 8);}
function readPCH(){return readBits('pch', 8);}

function readBit(name){
        return isNodeHigh(nodenames[name])?1:0;
}
function readBits(name, n){
	var res = 0;
	for(var i=0;i<n;i++){
		var nn = nodenames[name+i];
		res+=((isNodeHigh(nn))?1:0)<<i;
	}
	return res;
}

function writeDataBus(x){
	var recalcs = Array();
	for(var i=0;i<8;i++){
		var nn = nodenames['db'+i];
		var n = nodes[nn];
		if((x%2)==0) {n.pulldown=true; n.pullup=false;}
		else {n.pulldown=false; n.pullup=true;}
		recalcs.push(nn);
		x>>=1;
	}
	recalcNodeList(recalcs);
}

function mRead(a){
	if(memory[a]==undefined) return 0;
	else return memory[a];
}

function mWrite(a, d){memory[a]=d;}


function clkNodes(){
	var res = Array();
	res.push(943);
	for(var i in nodes[943].gates){
		var t = transistors[nodes[943].gates[i]];
		if(t.c1==npwr) res.push(t.c2);
		if(t.c2==npwr) res.push(t.c1);
	}
	hiliteNode(res);
}

function runChip(){
	var start = document.getElementById('start');
	var stop = document.getElementById('stop');
	start.style.visibility = 'hidden';
	stop.style.visibility = 'visible';
	running = true;
        steps();
}

function stopChip(){
	var start = document.getElementById('start');
	var stop = document.getElementById('stop');
	start.style.visibility = 'visible';
	stop.style.visibility = 'hidden';
	running = false;
}

function resetChip(){
	stopChip();
        setStatus('resetting 6502...');                          
	setTimeout(initChip,0);
}

function stepForward(){
	stopChip();
	step();
}

function chipStatus(){
	var ab = readAddressBus();
	var machine1 =
	        ' halfcyc:' + cycle +
	        ' phi0:' + readBit('clk0') +
                ' AB:' + hexWord(ab) +
	        ' D:' + hexByte(readDataBus()) +
	        ' RnW:' + readBit('rw');
	var machine2 =
	        ' PC:' + hexWord(readPC()) +
	        ' A:' + hexByte(readA()) +
	        ' X:' + hexByte(readX()) +
	        ' Y:' + hexByte(readY()) +
	        ' SP:' + hexByte(readSP()) +
	        ' ' + readPstring();
	var machine3 =
	        ' Sync:' + readBit('sync')
		' IRQ:' + readBit('irq') +
	        ' NMI:' + readBit('nmi');
	var machine4 =
	        ' IR:' + hexByte(255 - readBits('notir', 8)) +
	        ' idl:' + hexByte(255 - readBits('idl', 8)) +
	        ' alu:' + hexByte(255 - readBits('alu', 8)) +
	        ' TCstate:' + readBit('clock1') + readBit('clock2') +
                	readBit('t2') + readBit('t3') + readBit('t4') + readBit('t5');
        var machine5 =
                ' notRdy0:' + readBit('notRdy0') +
                ' fetch:'   + readBit('fetch') +
                ' clearIR:' + readBit('clearIR') +
                ' D1x1:'    + readBit('D1x1');
        setStatus(machine1 + "<br>" + machine2);
	if (loglevel>2 && ctrace) {
		console.log(machine1 + " " + machine2 + " " + machine3 + " " + machine4 + " " + machine5);
	}
	selectCell(ab);
}

function getMem(){
	var res = Array();
	for(var i=0;i<0x200;i++) res.push(mRead(i));
	return res;
}

function setMem(arr){
	for(var i=0;i<0x200;i++){mWrite(i, arr[i]); setCellValue(i, arr[i]);}
}

function hexWord(n){return (0x10000+n).toString(16).substring(1)}
function hexByte(n){return (0x100+n).toString(16).substring(1)}
