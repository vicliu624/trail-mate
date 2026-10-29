import L from 'leaflet';

// Leaflet 1.9.4 leaves a zoom completion timer alive after remove(). The
// public option avoids that callback on disposable React map views while
// retaining dragging, pan animation, zoom controls and fitBounds.
// Upstream: https://github.com/Leaflet/Leaflet/issues/9575
export function createTrackMap(host){
 return L.map(host,{preferCanvas:true,zoomAnimation:false}).setView([25,110],3);
}
