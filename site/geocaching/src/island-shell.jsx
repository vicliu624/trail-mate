import React from 'react';
import {createRoot} from 'react-dom/client';
import {flushSync} from 'react-dom';
import {Background, Button, Card, Divider, Icon, Title} from 'animal-island-ui';
import 'animal-island-ui/style';
import brandLogo from '../../../docs/images/logo_big.png';

const Label = ({name}) => <span data-label={name}/>;

export function mountIslandShell() {
  const host = document.createElement('div');
  host.id = 'geocaching-app';
  document.body.replaceChildren(host);
  flushSync(() => createRoot(host).render(<>
    <Background type="sprinkles" className="island-wallpaper" aria-hidden="true"/>
    <header className="header">
      <a className="brand" href="../"><img className="brand-logo" src={new URL(brandLogo, import.meta.url).href} alt=""/>Trail Mate</a>
      <span className="section-name"><Label name="title"/></span>
      <span id="connection-state" className="status" role="status" data-label="offline"/>
      <a className="source-link" href="https://github.com/vicliu624/trail-mate/tree/main/site/geocaching">GitHub</a>
    </header>
    <main className="workspace">
      <aside className="sidebar">
        <Title color="app-teal" size="middle"><Icon name="icon-map" size={24}/> <Label name="title"/></Title>
        <Card color="app-yellow" pattern="app-yellow" className="intro">
          <h1 data-label="heading"/><p data-label="intro"/>
        </Card>
        <p id="directory-status" className="help" data-label="noDirectory"/>
        <fieldset className="filters"><legend data-label="show"/>
          {['active','disabled','archived'].map((name,i)=><label key={name}><input type="checkbox" name="state" value={1<<i} defaultChecked={i<2}/><Label name={name}/></label>)}
        </fieldset>
        <Divider type="dashed-brown"/>
        <div className="results-heading"><h2 id="result-count" data-label="emptyCount"/><span id="selection-count"/></div>
        <p id="notice" role="status" data-label="start"/>
        <ol id="results" className="results"/>
        <div className="result-actions">
          <Button id="more" hidden><Label name="more"/></Button>
          <Button id="download-selected" disabled type="primary"><Label name="downloadSelected"/></Button>
          <Button id="download-partial" hidden><Label name="downloadPartial"/></Button>
        </div>
        <p className="scope" data-label="scope"/>
        <a className="library-credit" href="https://guokaigdg.github.io/animal-island-ui/">Animal Island UI</a>
      </aside>
      <section className="map-panel" aria-label="Geocaching map">
        <div id="map"/>
        <div className="map-tools"><Button id="search" type="primary" disabled icon={<Icon name="icon-map" size={20}/>}><Label name="search"/></Button>
          <Button id="locate" aria-label="Find my location" icon={<Icon name="location" size={22}/>}/></div>
        <p id="map-state" className="map-state" role="status"/>
        <Card id="detail" className="detail" hidden aria-live="polite">
          <Button id="close-detail" className="close" type="text" aria-label="Close details">×</Button>
          <span id="detail-state" className="eyebrow"/><h2 id="detail-name"/>
          <p id="detail-coordinates" className="coordinates"/><div id="detail-content"/>
          <Button id="download-one" type="primary" disabled><Label name="download"/></Button>
        </Card>
      </section>
    </main>
  </>));
}
