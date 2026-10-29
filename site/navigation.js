// Shared by the static homepage and the React tools. Routes are relative to the
// site root so GitHub Pages project prefixes and explicit language links survive.
const routes={home:'./',device:'device/',tracks:'tracks/',geocaching:'geocaching/',flash:'#webflasher'};
const copy={
 en:['Web tools','Device management','Tracks','Geocaching','Flash','Resources','Manage your device','Prepare a route','Find geocaches','Connect over USB to review settings, manage files and back up your device identity.','Import and edit routes, review elevation and timing, or browse shared tracks.','Explore geocaches by region and manage your own signed listings.','Open in your browser','Connection'],
 'zh-Hans':['网页工具','设备管理','轨迹','Geocaching','刷写固件','资源','管理设备','准备路线','寻找地理藏宝','通过 USB 检查设置、管理文件并备份设备身份。','导入和编辑路线，查看海拔与时间，或浏览共享轨迹。','按区域探索地理藏宝，并管理自己签名发布的条目。','在浏览器中使用','连接'],
 'zh-Hant-TW':['網頁工具','裝置管理','軌跡','Geocaching','燒錄韌體','資源','管理裝置','準備路線','尋找地理藏寶','透過 USB 檢查設定、管理檔案並備份裝置身分。','匯入及編輯路線，檢視海拔與時間，或瀏覽共享軌跡。','依區域探索地理藏寶，並管理自己簽署發佈的項目。','在瀏覽器中使用','連線'],
 ja:['Web ツール','デバイス管理','軌跡','Geocaching','書き込み','リソース','デバイスを管理','ルートを準備','ジオキャッシュを探す','USB で設定やファイルを管理し、デバイスの ID をバックアップします。','ルートの読み込み・編集、標高や時間の確認、共有軌跡の閲覧ができます。','地域別にジオキャッシュを探し、自分が署名した掲載情報を管理します。','ブラウザーで開く','接続'],
 ko:['웹 도구','기기 관리','트랙','Geocaching','펌웨어 설치','자료','기기 관리하기','경로 준비하기','지오캐시 찾기','USB로 설정과 파일을 관리하고 기기 신원을 백업하세요.','경로를 가져와 편집하고 고도와 시간을 확인하거나 공유 트랙을 둘러보세요.','지역별로 지오캐시를 탐색하고 직접 서명한 게시물을 관리하세요.','브라우저에서 열기','연결'],
 fr:['Outils Web','Gestion de l’appareil','Parcours','Geocaching','Installer','Ressources','Gérer votre appareil','Préparer un parcours','Trouver des géocaches','Connectez-vous en USB pour régler l’appareil, gérer les fichiers et sauvegarder son identité.','Importez et modifiez des parcours, consultez l’altitude et les horaires ou explorez les parcours partagés.','Explorez les géocaches par région et gérez vos propres annonces signées.','Ouvrir dans le navigateur','Connexion'],
 de:['Web-Werkzeuge','Geräteverwaltung','Tracks','Geocaching','Installieren','Ressourcen','Gerät verwalten','Route vorbereiten','Geocaches finden','Per USB Einstellungen prüfen, Dateien verwalten und die Geräteidentität sichern.','Routen importieren und bearbeiten, Höhe und Zeit prüfen oder geteilte Tracks durchsuchen.','Geocaches nach Region erkunden und eigene signierte Einträge verwalten.','Im Browser öffnen','Verbindung'],
 es:['Herramientas web','Gestión del dispositivo','Recorridos','Geocaching','Instalar','Recursos','Gestionar el dispositivo','Preparar una ruta','Buscar geocachés','Conecta por USB para revisar ajustes, gestionar archivos y respaldar la identidad del dispositivo.','Importa y edita rutas, consulta altitud y tiempos o explora recorridos compartidos.','Explora geocachés por región y gestiona tus propios anuncios firmados.','Abrir en el navegador','Conexión'],
 it:['Strumenti web','Gestione del dispositivo','Percorsi','Geocaching','Installa','Risorse','Gestisci il dispositivo','Prepara un percorso','Trova geocache','Collegati via USB per controllare le impostazioni, gestire i file e salvare l’identità del dispositivo.','Importa e modifica percorsi, consulta altitudine e tempi o esplora i percorsi condivisi.','Esplora le geocache per regione e gestisci i tuoi annunci firmati.','Apri nel browser','Connessione'],
 ru:['Веб-инструменты','Управление устройством','Треки','Geocaching','Установка','Ресурсы','Управление устройством','Подготовка маршрута','Поиск тайников','Подключитесь по USB для настройки, управления файлами и резервного копирования идентификатора устройства.','Импортируйте и редактируйте маршруты, изучайте высоты и время или просматривайте общие треки.','Ищите тайники по регионам и управляйте своими подписанными объявлениями.','Открыть в браузере','Подключение'],
 ar:['أدوات الويب','إدارة الجهاز','المسارات','Geocaching','تثبيت','الموارد','إدارة جهازك','إعداد مسار','البحث عن مخابئ','اتصل عبر USB لمراجعة الإعدادات وإدارة الملفات ونسخ هوية الجهاز احتياطياً.','استورد المسارات وعدّلها وراجع الارتفاع والتوقيت أو تصفّح المسارات المشتركة.','استكشف المخابئ حسب المنطقة وأدر قوائمك الموقّعة.','فتح في المتصفح','الاتصال'],
};
const keys=['tools','device','tracks','geocaching','flash','resources','deviceHeading','tracksHeading','geocachingHeading','deviceHelp','tracksHelp','geocachingHelp','open','connection'];
export function navigationText(locale){return Object.fromEntries(keys.map((key,index)=>[key,(copy[locale]??copy.en)[index]]));}
export function navigationHref(page,locale,root){
 const url=new URL(routes[page],root);
 url.searchParams.set('lang',locale);
 return url.href;
}
export function applySiteNavigation(root,locale,siteRoot){
 const text=navigationText(locale);
 root.querySelectorAll('[data-site-text]').forEach(element=>{element.textContent=text[element.dataset.siteText];});
 root.querySelectorAll('[data-site-nav]').forEach(element=>element.setAttribute('aria-label',text.tools));
 root.querySelectorAll('[data-site-link]').forEach(element=>{element.href=navigationHref(element.dataset.siteLink,locale,siteRoot);});
}
export function installNavigationDismissal(root){
 const close=event=>root.querySelectorAll('.site-resources[open]').forEach(menu=>{
  if(event.type==='keydown'){
   if(event.key==='Escape'){menu.open=false;menu.querySelector('summary').focus();}
  }else if(!menu.contains(event.target))menu.open=false;
 });
 root.addEventListener('keydown',close);
 root.addEventListener('click',close);
 return ()=>{root.removeEventListener('keydown',close);root.removeEventListener('click',close);};
}
