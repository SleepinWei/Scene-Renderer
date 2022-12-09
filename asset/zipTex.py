import zipfile
import os

path = "./model/textures"
zip_file = "textures.zip"
zip = zipfile.ZipFile(zip_file,'w',zipfile.ZIP_DEFLATED)

files = os.listdir(path)
for index,file in enumerate(files):
    if file.endswith("dds"):
        print(index / len(files))
        zip.write(path + "/" + file)

zip.close();
print("Done")
