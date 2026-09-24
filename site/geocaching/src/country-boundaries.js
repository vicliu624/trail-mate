const polygons = feature => feature.geometry.type === 'Polygon' ? [feature.geometry.coordinates] : feature.geometry.coordinates;
const wrap = longitude => ((longitude + 180) % 360 + 360) % 360 - 180;
const globalRing = ring => ring.some(p=>p[0]===-180) && ring.some(p=>p[0]===180) && ring.some(p=>Math.abs(p[0])<90);

// Work in a continuous longitude interval, including islands across ±180°.
export function countryBounds(feature) {
  const points = polygons(feature).flatMap(polygon => polygon[0]);
  const longitudes = points.map(point => wrap(point[0])).sort((a,b) => a-b);
  let gap = -1, start = 0;
  for (let i=0; i<longitudes.length; i++) {
    const next = i+1<longitudes.length ? longitudes[i+1] : longitudes[0]+360;
    if (next-longitudes[i]>gap) { gap=next-longitudes[i]; start=next; }
  }
  const west = wrap(start);
  // Antarctica spans the pole; don't zoom to an arbitrary longitude gap.
  const global=feature.id==='ATA' || polygons(feature).some(polygon=>globalRing(polygon[0]));
  return [Math.min(...points.map(p=>p[1])), global?-180:west,
    Math.max(...points.map(p=>p[1])), global?180:west+360-gap];
}

function ringContains(ring, latitude, longitude) {
  const points = globalRing(ring)?ring:[];
  for (const point of globalRing(ring)?[]:ring) {
    const previous = points.length ? points[points.length-1][0] : point[0];
    points.push([point[0]+360*Math.round((previous-point[0])/360),point[1]]);
  }
  const center = points.reduce((sum,p)=>sum+p[0],0)/points.length;
  const x = globalRing(ring)?wrap(longitude):longitude+360*Math.round((center-longitude)/360), y = latitude;
  let inside = false;
  for (let i=0,j=points.length-1;i<points.length;j=i++) {
    const [ax,ay]=points[j], [bx,by]=points[i];
    const cross=(x-ax)*(by-ay)-(y-ay)*(bx-ax);
    if (Math.abs(cross)<1e-9 && x>=Math.min(ax,bx)-1e-9 && x<=Math.max(ax,bx)+1e-9 &&
        y>=Math.min(ay,by)-1e-9 && y<=Math.max(ay,by)+1e-9) return true;
    if ((ay>y)!==(by>y) && x<(bx-ax)*(y-ay)/(by-ay)+ax) inside=!inside;
  }
  return inside;
}

export function regionDisplayGeometry(feature, center) {
  const coordinates=polygons(feature).map(polygon=>{
    if(globalRing(polygon[0]))return polygon;
    const first=polygon[0][0][0];
    return polygon.map(ring=>ring.map(([x,y])=>{
      const longitude=x+360*Math.round((first-x)/360);
      return [longitude+360*Math.round((center-first)/360),y];
    }));
  });
  return {...feature,geometry:{type:'MultiPolygon',coordinates}};
}

export function countryContains(feature, latitude, longitude) {
  if (!Number.isFinite(latitude) || !Number.isFinite(longitude)) return false;
  return polygons(feature).some(polygon => ringContains(polygon[0],latitude,longitude) &&
    !polygon.slice(1).some(hole=>ringContains(hole,latitude,longitude)));
}

export function countryMatches(feature, text) {
  const normalize = value => value.normalize('NFD').replace(/\p{Diacritic}/gu,'').toLocaleLowerCase();
  const iso=feature.properties.iso2;
  const shortName=iso && /^[A-Z]{2}$/.test(iso)?new Intl.DisplayNames(['zh-Hans'],{type:'region'}).of(iso):'';
  return normalize([feature.properties.name,feature.properties.zh,shortName,iso,feature.id].join(' ')).includes(normalize(text.trim()));
}
