import React,{useEffect,useRef} from 'react';
import {ManagementLanguage,useManagementLanguage} from './i18n/management-language.jsx';
import {installNavigationDismissal,navigationHref,navigationText} from './navigation.js';
import brand from '../docs/images/logo_big.png';
import './site-header.css';
import './island-controls.css';

export function SiteHeader({page,children,...attributes}){
 const {locale}=useManagementLanguage(),text=navigationText(locale),root=new URL('../',location.href);
 const header=useRef();
 useEffect(()=>installNavigationDismissal(header.current.ownerDocument),[]);
 return <header className="topbar site-header" ref={header} {...attributes}>
  <div className="site-header-main">
   <a className="brand site-brand" href={navigationHref('home',locale,root)}><img className="brand-logo" src={new URL(brand,import.meta.url).href} alt=""/><span>Trail Mate</span></a>
   <nav className="site-primary" aria-label={text.tools}>
    {['device','tracks','geocaching'].map(key=><a key={key} href={navigationHref(key,locale,root)} aria-current={page===key?'page':undefined}>{text[key]}</a>)}
   </nav>
   <div className="site-utilities">
    <a className="site-flash" href={navigationHref('flash',locale,root)}>{text.flash}</a>
    <details className="site-resources"><summary>{text.resources}</summary><div>
     <a href="https://github.com/vicliu624/trail-mate">GitHub</a>
     <a href="https://github.com/vicliu624/trail-mate/wiki">Wiki</a>
    </div></details>
    <ManagementLanguage page={page}/>
   </div>
  </div>
  {children&&<div className="site-page-actions" role="group" aria-label={text.connection}>{children}</div>}
 </header>;
}
