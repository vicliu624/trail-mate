// Round the center to roughly kilometre precision before querying directories.
export function nearbyBounds(latitude, longitude, accuracy = 0) {
  if (!Number.isFinite(latitude) || !Number.isFinite(longitude) || Math.abs(latitude)>90) throw Error('Invalid location');
  const lat=Math.round(latitude*100)/100, lon=Math.round(longitude*100)/100;
  const radiusKm=Math.max(25,Math.min(200,Number.isFinite(accuracy)?accuracy/1000:25));
  const dy=radiusKm/111.2, dx=Math.min(180,dy/Math.max(0.001,Math.cos(lat*Math.PI/180)));
  return [Math.max(-90,lat-dy),lon-dx,Math.min(90,lat+dy),lon+dx];
}

export function requestNearbyPosition(geolocation) {
  return new Promise((resolve,reject)=>{
    if (!geolocation) { reject(Error('unavailable')); return; }
    // Native timeout may exclude time spent waiting on the permission prompt.
    const timer=setTimeout(()=>reject(Error('timeout')),10000);
    const fail=error=>{clearTimeout(timer);reject(error);};
    try {
      geolocation.getCurrentPosition(position=>{
        clearTimeout(timer);
        try {resolve(nearbyBounds(position.coords.latitude,position.coords.longitude,position.coords.accuracy));}
        catch(error){reject(error);}
      },fail,{enableHighAccuracy:false,timeout:10000,maximumAge:300000});
    } catch(error) { fail(error); }
  });
}
