import React from 'react';
import {MapIcon, FileIcon, RefreshIcon, PaintbrushIcon, BookIcon, SettingsIcon, ChatIcon, LocationIcon} from 'naive-icons';

const icons = {
  'icon-map': MapIcon, page: FileIcon, 'icon-variant': RefreshIcon,
  'icon-design': PaintbrushIcon, 'icon-encyclopedia': BookIcon,
  'icon-diy': SettingsIcon, 'icon-chat': ChatIcon, location: LocationIcon,
};

// Keep the site's semantic icon names while using Animal Island UI 2's icon library.
export function Icon({name, ...props}) {
  const Component = icons[name];
  return Component ? <Component {...props}/> : null;
}
