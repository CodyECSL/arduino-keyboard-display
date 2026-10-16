"""Convert the reference 3MF (Printables model 1442589) to an ASCII STL.

OpenSCAD 2021.01 cannot import 3MF. Usage:
    python3 -I mount/tools/ref_3mf_to_stl.py <file.3mf> mount/ref/rim.stl
"""
import re
import sys
import zipfile

src, dst = sys.argv[1], sys.argv[2]
xml = zipfile.ZipFile(src).read("3D/3dmodel.model").decode()
body = re.search(r'<object id="2".*?</object>', xml, re.S).group(0)
verts = [tuple(map(float, m)) for m in
         re.findall(r'<vertex x="([^"]+)" y="([^"]+)" z="([^"]+)"', body)]
tris = [tuple(map(int, m)) for m in
        re.findall(r'<triangle v1="(\d+)" v2="(\d+)" v3="(\d+)"', body)]
with open(dst, "w") as f:
    f.write("solid rim\n")
    for tri in tris:
        f.write("facet normal 0 0 0\nouter loop\n")
        for i in tri:
            f.write("vertex %f %f %f\n" % verts[i])
        f.write("endloop\nendfacet\n")
    f.write("endsolid rim\n")
print(len(verts), "vertices,", len(tris), "triangles")
