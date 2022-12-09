import os

path = "./model/textures"
exe_path = '"E:\\NVIDIA Corporation\\NVIDIA Texture Tools\\nvcompress.exe"'

for file in os.listdir(path):
    if file.endswith("png"):
        filepath = path +"/" + file

        os.system(f"{exe_path} -color -bc1 {filepath} {filepath[:-3]}dds")