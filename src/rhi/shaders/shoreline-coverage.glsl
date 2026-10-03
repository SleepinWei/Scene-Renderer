float shorelineCoverage(float height,float normalY,float proximity,float range,float slopeMin,float slopeMax){
    float altitude=1.-smoothstep(range*.55,range,height);
    float slope=smoothstep(slopeMin,slopeMax,abs(normalY));
    return proximity*altitude*slope*smoothstep(-4.,-.5,height);
}
