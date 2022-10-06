#!/usr/bin/python3
import libvirt
import libxml2
import sys
import os


if len(sys.argv) != 2:
    print('USAGE: vif-stats domain-name')
    sys.exit(1)

name = sys.argv[1];

# open libvirt connection
conn = libvirt.openReadOnly(None)
if conn == None:
    print('Failed to open connection to the hypervisor')
    sys.exit(1)

# get domain
try:
    dom = conn.lookupByName(name)
except:
    print('Failed to find domain ' + name)
    sys.exit(1)

# get domain config
xml = dom.XMLDesc(0)
try:
    doc = libxml2.parseDoc(xml)
except:
    print('Failed to parse domain xml')
    sys.exit(1)

ctx = doc.xpathNewContext()
vifs = {}
# for each interface defined in domain config, get stats
try:
    ret = ctx.xpathEval("/domain/devices/interface")
    for node in ret:
        for child in node.children:
            if child.name == "target":
                vif = child.prop("dev")
                vif_stats = dom.interfaceStats(vif)
                vifs[vif] = vif_stats
finally:
    if ctx != None:
        ctx.xpathFreeContext()
    if doc != None:
        doc.freeDoc()

if len(vifs) == 0:
    print("No vifs defined for domain %s" % name)
    sys.exit(1)

names = vifs.keys()
output = ""
# create metrics XML for TX/RX bytes and errors for each interface
for name in names:
    # rx_bytes
    output += "<metric type=\"uint64\" context=\"vm\" id=\"" + str(dom.ID()) + "\">\n"
    output += "  <name>" + name + "-rx_bytes</name>\n"
    output += "  <value>" + str(vifs[name][0]) + "</value>\n"
    output += "</metric>\n"

    # rx_drop
    output += "<metric type=\"uint64\" context=\"vm\" id=\"" + str(dom.ID()) + "\">\n"
    output += "  <name>" + name + "-rx_err</name>\n"
    output += "  <value>" + str(vifs[name][2]) + "</value>\n"
    output += "</metric>\n"

    # rx_bytes
    output += "<metric type=\"uint64\" context=\"vm\" id=\"" + str(dom.ID()) + "\">\n"
    output += "  <name>" + name + "-tx_bytes</name>\n"
    output += "  <value>" + str(vifs[name][4]) + "</value>\n"
    output += "</metric>\n"

    # rx_bytes
    output += "<metric type=\"uint64\" context=\"vm\" id=\"" + str(dom.ID()) + "\">\n"
    output += "  <name>" + name + "-tx_err</name>\n"
    output += "  <value>" + str(vifs[name][6]) + "</value>\n"
    output += "</metric>\n"

del dom
del conn

# print metrics XML to stdout
print(output)

sys.exit(0)
