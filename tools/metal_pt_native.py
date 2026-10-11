"""Generate a Metal RT variant from SPIRV-Cross's shared PT integrator.

Only intersectSceneMode is replaced. Resource names are discovered by type,
and the acceleration structure is threaded through its call graph. Fail closed
if generated signatures change; never duplicate BSDF/medium/integrator math.
"""
import re


def closing(text, start, left='(', right=')'):
    depth = 0
    for i in range(start, len(text)):
        if text[i] == left:
            depth += 1
        elif text[i] == right:
            depth -= 1
            if depth == 0:
                return i
    raise ValueError('Unbalanced generated Metal source')


def native_variant(text):
    functions = {}
    for m in re.finditer(r'^\w+(?: \w+)* (\w+)\(', text, re.MULTILINE):
        end = closing(text, m.end()-1)
        body = end+1
        while text[body].isspace():
            body += 1
        if text[body] != '{':
            continue
        stop = closing(text, body, '{', '}')
        functions[m[1]] = (m.end()-1, end, body, stop)
    target = functions['intersectSceneMode']
    signature = text[target[0]+1:target[1]]
    params = signature.split(', ')
    variables = {p.split('&')[0].strip(): p.split('&')[1].strip() for p in params if '&' in p}
    def argument_call(name, replacements):
        a, b, _, _ = functions[name]
        arguments = []
        for param in text[a+1:b].split(', '):
            kind, variable = param.split('&')
            variable = variable.strip()
            if variable in replacements:
                arguments.append(replacements[variable])
            else:
                arguments.append(variables[kind.strip()])
        return name+'('+', '.join(arguments)+')'
    triangle = argument_call('triangleHit', dict(id='tri', instance='iid', o='lo', d='ld', maximum='limit', hit='candidate'))
    opaque = argument_call('shadowOpaqueHit', dict(hit='candidate'))
    instances = variables['device Instances']
    body = '''{
    ++rayCount;
    hit.primitive=0xffffffffu;hit.distance=maximum;opaque=false;
    if(p.acceleration.x==0u || maximum<rayEpsilon)return false;
    metal::raytracing::ray ray(o,d,rayEpsilon,maximum);
    metal::raytracing::intersection_params params;
    params.force_opacity(metal::raytracing::forced_opacity::non_opaque);
    metal::raytracing::intersection_query<metal::raytracing::instancing,metal::raytracing::triangle_data> query(ray,nativeScene,0xff,params);
    while(query.next()) {
        uint iid=query.get_candidate_instance_id();
        uint tri=INSTANCES.instances[iid].info.w+query.get_candidate_primitive_id();
        float3 lo=(INSTANCES.instances[iid].inverse*float4(o,1)).xyz;
        float3 ld=(INSTANCES.instances[iid].inverse*float4(d,0)).xyz;
        float limit=mode==2u?maximum:hit.distance;Hit candidate;
        if(!TRIANGLE)continue;
        if(mode==1u || (mode==2u && OPAQUE)) {hit=candidate;opaque=true;query.abort();return true;}
        if(candidate.distance<hit.distance || (candidate.distance==hit.distance && candidate.primitive<hit.primitive))hit=candidate;
        // Do not commit hardware t: exact shared triangle math owns t and ties.
        // This also keeps looking for opaque blockers behind transparent hits.
    }
    return hit.primitive!=0xffffffffu;
}'''.replace('INSTANCES', instances).replace('TRIANGLE', triangle).replace('OPAQUE', opaque).replace('.distance', '._distance').replace('.inverse', '._inverse')
    # Find every ancestor of the replaced intersection entry.
    required = {'intersectSceneMode'}
    while True:
        previous = len(required)
        for name, (_, _, a, b) in functions.items():
            if any(re.search(r'\b'+re.escape(child)+r'\(', text[a:b]) for child in required):
                required.add(name)
        if len(required) == previous:
            break
    if 'main0' not in required:
        raise ValueError('Native RT intersection has no kernel caller')
    edits = [(target[2], target[3]+1, body)]
    for name in required:
        definition = functions[name][0]
        for m in re.finditer(r'\b'+re.escape(name)+r'\(', text):
            start = m.end()-1
            end = closing(text, start)
            if target[2] <= start <= target[3]:
                continue
            if start == definition:
                value = ', metal::raytracing::instance_acceleration_structure nativeScene'
                if name == 'main0':
                    value += ' [[buffer(24)]]'
            else:
                value = ', nativeScene'
            edits.append((end, end, value))
    for a, b, value in sorted(edits, reverse=True):
        text = text[:a]+value+text[b:]
    return '#include <metal_raytracing>\n'+text
