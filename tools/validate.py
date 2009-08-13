from xml.parsers.xmlproc import xmlproc
from xml.parsers.xmlproc import xmlval
from xml.parsers.xmlproc import xmldtd

def validate_xml(xml_filename, dtd_filename):
   """Validate a given XML file with a given external DTD.
      If the XML file is not valid, an exception will be 
      printed with an error message.
   """
   dtd = xmldtd.load_dtd(dtd_filename)
   parser = xmlproc.XMLProcessor()
   parser.set_application(xmlval.ValidatingApp(dtd, parser))
   parser.dtd = dtd
   parser.ent = dtd
   parser.parse_resource(xml_filename)


if __name__ == "__main__":
   import sys
   xml_filename, dtd_filename = sys.argv[1], sys.argv[2]
   validate_xml(xml_filename, dtd_filename)
