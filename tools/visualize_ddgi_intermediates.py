#!/usr/bin/env python3
"""Visualize actual native DDGI readbacks with shared, explicit display scales."""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('prefix', type=Path)
    args = parser.parse_args()
    prefix = str(args.prefix)
    setup = json.loads(Path(prefix+'-ddgi-reference.json').read_text())
    volume = setup['probe_volume']
    width, height = setup['width'], setup['height']
    archive_path = Path(prefix+'-intermediates.npz')
    archive = np.load(archive_path) if archive_path.exists() else None
    arrays, hashes = {}, {}

    def load(name, shape):
        path = Path(prefix+'-'+name+'.bin')
        data = np.fromfile(path, dtype='<f4') if path.exists() else archive[name]
        data = data.reshape(shape)
        if not np.isfinite(data).all():
            raise ValueError('Nonfinite intermediate '+name)
        arrays[name] = data
        hashes[name] = hashlib.sha256(data.astype('<f4').tobytes()).hexdigest()
        return data

    field = load('probe-field', (setup['probe_count'], volume['stride_vec4'], 4))
    query = load('surface-query', (height, width, 4))
    position = load('surface-position', (height, width, 4))
    normal = load('surface-normal', (height, width, 4))
    albedo = load('surface-albedo', (height, width, 4))
    emissive_ao = load('surface-emissive-ao', (height, width, 4))
    font = ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial.ttf', 17)
    small = ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial.ttf', 14)
    background = '#181d24'
    palette = np.array([[.05,.05,.22],[.12,.37,.66],[.25,.72,.66],[.96,.87,.33],[.94,.25,.17]])

    def heat(values):
        values = np.clip(values, 0, 1)
        rgb = np.stack([np.interp(values, np.linspace(0, 1, len(palette)), palette[:,i]) for i in range(3)], axis=-1)
        return Image.fromarray((rgb*255+.5).astype(np.uint8))

    def mapped(values):
        rgb = np.clip((1-np.exp(-np.maximum(values, 0)*setup['exposure']))**(1/setup['gamma']), 0, 1)
        return Image.fromarray((rgb*255+.5).astype(np.uint8))

    def enlarged(image, size):
        return image.resize(size, Image.Resampling.NEAREST)

    captures = sorted(setup['probe_captures'], key=lambda item: {423:0,663:1,295:2}.get(item['index'],item['index']))
    if not captures:
        raise ValueError('No selected final-sweep captures; recapture Sponza with the updated diagnostic entry')
    tile, label_w, row_h = 96, 280, 350
    capture_sheet = Image.new('RGB', (label_w+6*(tile+4), len(captures)*row_h+65), background)
    draw = ImageDraw.Draw(capture_sheet)
    draw.text((12, 10), f'Actual {setup.get("probe_capture_backend","raster")} probe input / {volume["capture_resolution"]} x {volume["capture_resolution"]} per face', fill='white', font=font)
    draw.text((12, 35), 'Rows per probe: outgoing RGB / radial distance / hit classification', fill='#c2ccd8', font=small)
    nx, ny, nz = volume['counts']
    max_distance = volume['max_distance']
    probe_details = []
    for row, capture in enumerate(captures):
        index = capture['index']
        raw = load('probe-capture-'+str(index), (capture['height'], capture['width'], 4))
        grid = np.array([index%nx, (index//nx)%ny, index//(nx*ny)])
        p = np.array(volume['origin'])+grid*volume['spacing']
        state = field[index, 0]
        y = row*row_h+65
        draw.text((12, y+25), f'Probe {index} / {tuple(int(value) for value in p)} m', fill='white', font=font)
        draw.text((12, y+48), f'Capture frame {capture["frame"]} / cache frame {setup["raster_frames"]}', fill='#c2ccd8', font=small)
        draw.text((12, y+72), 'Active' if state[0]>.5 else 'Rejected', fill='#35d389' if state[0]>.5 else '#ef6262', font=font)
        draw.text((12, y+95), f'Backface solid angle: {state[2]*100:.2f}%', fill='white', font=small)
        draw.text((12, y+145), 'Distance: log(1+d), 0..45 m', fill='#c2ccd8', font=small)
        draw.text((12, y+245), 'Gray: front / red: back / blue: miss', fill='#c2ccd8', font=small)
        for face, label in enumerate(['+X', '-X', '+Y', '-Y', '+Z', '-Z']):
            n = volume['capture_resolution']
            face_data = raw[:, face*n:(face+1)*n]
            distance = face_data[:, :, 3]
            mask = np.empty((n, n, 3), dtype=np.uint8);mask[:]=[164,178,190]
            mask[distance<0] = [239,98,98];mask[distance==0] = [58,126,226]
            distance_image = np.array(heat(np.log1p(np.abs(distance))/np.log1p(max_distance)))
            distance_image[distance==0] = [40,47,59]
            x = label_w+face*(tile+4)
            draw.text((x+tile//2-12, y+3), label, fill='white', font=font)
            capture_sheet.paste(enlarged(mapped(face_data[:, :, :3]), (tile,tile)), (x,y+25))
            capture_sheet.paste(enlarged(Image.fromarray(distance_image), (tile,tile)), (x,y+125))
            capture_sheet.paste(enlarged(Image.fromarray(mask), (tile,tile)), (x,y+225))
        probe_details.append({'index':index,'position':p.tolist(),'capture_frame':capture['frame'],
                              'active':bool(state[0]>.5),'visits':float(state[1]),'backface_solid_angle_fraction':float(state[2]),
                              'raw_front_samples':int((raw[:,:,3]>0).sum()),'raw_back_samples':int((raw[:,:,3]<0).sum()),
                              'raw_miss_samples':int((raw[:,:,3]==0).sum())})
    capture_sheet.save(prefix+'-intermediate-capture.png')
    capture_sheet.crop((0,0,capture_sheet.width,row_h+65)).save(prefix+'-intermediate-capture-detail.png')

    tile, label_w, row_h = 180, 280, 240
    cache_sheet = Image.new('RGB', (label_w+3*(tile+20), len(captures)*row_h+80), background)
    draw = ImageDraw.Draw(cache_sheet)
    for column, label in enumerate(['Directional E/pi (8 x 8)', 'Mean distance (16 x 16)', 'Distance sigma (16 x 16)']):
        draw.text((label_w+column*(tile+20), 10), label, fill='white', font=small)
    for row, capture in enumerate(captures):
        index = capture['index'];y = row*row_h+45
        irradiance = field[index, 1:65, :3].reshape(8,8,3)
        moments = field[index, 65:321, :2].reshape(16,16,2)
        sigma = np.sqrt(np.maximum(moments[:,:,1]-moments[:,:,0]**2, 0))
        draw.text((12,y+15), f'Probe {index} / cached after {setup["raster_frames"]} frames', fill='white', font=small)
        draw.text((12,y+40), 'ACTIVE' if field[index,0,0]>.5 else 'REJECTED: excluded by queries', fill='#35d389' if field[index,0,0]>.5 else '#ef6262', font=small)
        for column, image in enumerate([mapped(irradiance), heat(np.log1p(moments[:,:,0])/np.log1p(max_distance)),
                                         heat(np.log1p(sigma)/np.log1p(max_distance/2))]):
            cache_sheet.paste(enlarged(image,(tile,tile)), (label_w+column*(tile+20), y))
    draw.text((12,cache_sheet.height-40), f'RGB: shared exposure {setup["exposure"]:.1f} / distance log scale 0..{max_distance:g} m / sigma log scale 0..{max_distance/2:g} m', fill='white', font=small)
    draw.text((12,cache_sheet.height-20), 'Octahedral direction tiles; rejected caches can contain values but never contribute to surface queries.', fill='#c2ccd8', font=small)
    cache_sheet.save(prefix+'-intermediate-cache.png')

    # This is a labelled CPU derivation from native GBuffer and query values,
    # matching the positive DDGI diffuse term; it is not the additive replacement
    # delta, which also subtracts the previous ambient / sky diffuse.
    valid = (position[:,:,3]>0)&(query[:,:,3]>0)
    N = normal[:,:,:3]/np.maximum(np.linalg.norm(normal[:,:,:3],axis=2,keepdims=True),1e-8)
    V = np.array(setup['camera'])-position[:,:,:3]
    V /= np.maximum(np.linalg.norm(V,axis=2,keepdims=True),1e-8)
    cosine = np.clip((N*V).sum(axis=2,keepdims=True), 0, 1)
    metallic = albedo[:,:,3:4];F0 = .04*(1-metallic)+albedo[:,:,:3]*metallic
    F = F0+(np.maximum(1-normal[:,:,3:4],F0)-F0)*(1-cosine)**5
    diffuse = albedo[:,:,:3]*(1-F)*(1-metallic)
    contribution = query[:,:,:3]*diffuse*emissive_ao[:,:,3:4]*query[:,:,3:4]*volume['intensity']
    contribution[~valid]=0
    arrays['derived-diffuse-contribution']=contribution.astype(np.float32)
    surface_sheet = Image.new('RGB', (width*3,height+65), background)
    draw = ImageDraw.Draw(surface_sheet)
    beauty_path = Path(prefix+'-raster-ddgi.png')
    beauty = Image.open(beauty_path).convert('RGB') if beauty_path.exists() else Image.fromarray(archive['beauty-rgb8'])
    arrays['beauty-rgb8'] = np.array(beauty)
    for column, (label,image) in enumerate([('GPU surface query / incident E/pi',mapped(query[:,:,:3])),
                                          ('Derived DDGI diffuse / material-weighted',mapped(contribution)),
                                          ('Final Raster / direct + GI + specular',beauty)]):
        surface_sheet.paste(image,(column*width,40));draw.text((column*width+10,10),label,fill='white',font=font)
    draw.text((12,height+45), 'Same exposure / TAA OFF / sky has no surface query / middle panel excludes the ambient replacement subtraction', fill='#c2ccd8',font=small)
    surface_sheet.save(prefix+'-intermediate-surface.png')
    if archive is not None:
        archive.close()
    np.savez_compressed(archive_path, **arrays)
    report={'scene':setup['scene'],'size':[width,height],'exposure':setup['exposure'],'probe_captures':probe_details,
            'surface_query_pixels':setup['surface_query_pixels'],'query_max_rgb':float(query[:,:,:3].max()),
            'coverage_range_valid': [float(query[:,:,3][valid].min()),float(query[:,:,3][valid].max())],
            'raw_sha256':hashes,'field_frozen_unchanged':setup['surface_diagnostics']['frozen_probe_field_unchanged'],
            'raw_capture_scope':('RGB is shared PT path radiance including sky on misses; alpha is signed first-hit distance. Miss alpha=0; integration does not add sky again. Primary solar misses are excluded.' if setup.get('probe_capture_backend')=='shared-gpu-pt' else 'RGB is raster outgoing radiance; alpha is signed radial distance. Misses have RGBA=0; sky is added by integration, not stored in scratch RGB. Backface radiance is rejected by integration.'),
            'cache_scope':'Capture panels are sampled during the final sweep; cache and surface queries are from the final frame. The cache includes temporal history, not just the displayed capture.',
            'surface_scope':'Dense GPU ddgiQuery; bias compensated to match composition. Positive material-weighted DDGI term derived on CPU from native readbacks; final beauty remains native GPU. AO/RSM/TAA are off.',
            'distance_display':'Shared logarithmic colour scale. Mean distance in 0..45 m; sigma=sqrt(max(m2-m1^2,0)) in 0..22.5 m. Misses in scratch distance are dark gray, not zero-range hits.'}
    Path(prefix+'-intermediate-summary.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    main()
