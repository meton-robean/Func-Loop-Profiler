#!/usr/bin/python
#encoding=utf-8

#usage: ./callgraph_v2.py addr2line test log.out callgraph.png
#handle data type eg. 4005b3-2-10%-20%--4004f4-1-2.3%-3.8%
import re
import os
import sys

addr2line = sys.argv[1]
exefile = sys.argv[2]
inputfile = sys.argv[3]
outputfile = sys.argv[4]

addr2name = {}
name2addr={}
addr2type={}
addr2lebel={} 
edges = set()

def get_node_index(name):
  if nodes.count(name) == 0:
    nodes.append(name)
  return nodes.index(name)


f = open(inputfile, 'r')
addrs = []
while True:
  line = f.readline().strip('\r\n')
  if not line: break
  #print line
  tmp_list=line.split("--")
  caller=tmp_list[0].split("-")[0]
  callee=tmp_list[1].split("-")[0]
  addrs.append(caller)
  addrs.append(callee)
  edges.add((caller, callee))
  addr2type[caller]=tmp_list[0].split("-")[1]
  addr2type[callee]=tmp_list[1].split("-")[1]
  addr2lebel[caller]=tmp_list[0].split("-")[0]+'\r\n('+tmp_list[0].split("-")[-2]+','+tmp_list[0].split("-")[-1]+')'
  addr2lebel[callee]=tmp_list[1].split("-")[0]+'\r\n('+tmp_list[1].split("-")[-2]+','+tmp_list[1].split("-")[-1]+')'

f.close()
# print "addrs:",addrs
# print "edges:",edges
# print "addr2type:",addr2type
# print "addr2lebel:",addr2lebel

addr2lebel2={}

for addr in addrs:
  if addr2type[addr]=='1':
    isLineNumber=True
    for line in os.popen('%s -f -C -e %s %s' % (addr2line, exefile, addr)).readlines():
      isLineNumber=not isLineNumber
      if isLineNumber:
        continue
      name=line.strip('\r\n')
      #print name
    addr2name[addr]=name
    name2addr[name]=addr
    addr2lebel2[addr]=addr2name[addr]+addr2lebel[addr][addr2lebel[addr].index('\r'):]

  if addr2type[addr]=='2':
    isLineNumber=False
    namespace_pattern = re.compile('^[\w_]+::[\w_]+::')
    for line in os.popen('%s -f -C -e %s %s' % (addr2line, exefile, addr)).readlines():
      isLineNumber=not isLineNumber
      if isLineNumber:
        continue
      name=line.strip('\r\n')
      #print name
      str_list=name.split("/")
      name_and_line=str_list[-1]
      if name_and_line[-1]==')':
        try:
          name_and_line=name_and_line[:name_and_line.index('(')]
        except:pass
      #print name_and_line
      addr2name[addr]=name_and_line
      name2addr[name_and_line]=addr
      addr2lebel2[addr]=addr2name[addr]+addr2lebel[addr][addr2lebel[addr].index('\r'):]
nodes=set(addr2name.values())

# print "addr2name:",addr2name
# print "name2addr:",name2addr
# print "nodes:",nodes



f = open('callgraph.dot', 'w')
f.write('digraph G {\n');
nodeIds = {}
i=0
for node in nodes:
  if (node=='__cyg_profile_func_exit') or (node=='__cyg_profile_func_enter'):
    pass
  else:
    nodeId = 'node' + str(i)
    if addr2type[name2addr[node]]=='1':
      f.write('%s [ label="%s" ];\n' % (nodeId,node ))
    else:
      f.write('%s [ label="%s",shape="ellipse",color = "red"];\n' % (nodeId, addr2lebel2[name2addr[node]]))
    nodeIds[node] = nodeId
    i += 1


output_edges = []
for edge in edges:
  if (addr2name[edge[0]]=='__cyg_profile_func_exit')  or (addr2name[edge[0]]=='__cyg_profile_func_enter') or (addr2name[edge[1]]=='__cyg_profile_func_exit') or (addr2name[edge[1]]=='__cyg_profile_func_enter'):
    pass
  else:
    caller_id = nodeIds[addr2name[edge[0]]]
    callee_id = nodeIds[addr2name[edge[1]]]
    if output_edges.count((caller_id, callee_id)) == 0:
      f.write('%s -> %s\n' % (caller_id, callee_id))
      output_edges.append((caller_id, callee_id))
f.write('}\n');
f.close()

os.system('dot -Tpng -Nshape=box -Nfontsize=50 -Gdpi=300 callgraph.dot -o ' + outputfile)
print"done! generate callgraph.png  "
