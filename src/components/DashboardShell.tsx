import type { ReactNode } from 'react';
import {
  Activity,
  Bell,
  LogOut,
  RefreshCw,
  SlidersHorizontal,
  Sprout,
  Waves,
  Wifi,
  WifiOff,
} from 'lucide-react';
import type { DashboardTab } from '../types/dashboard';

interface DashboardShellProps {
  user: string;
  connected: boolean;
  lastUpdated: Date | null;
  activeTab: DashboardTab;
  attentionCount: number;
  message: string;
  onTabChange: (tab: DashboardTab) => void;
  onLogout: () => void;
  onRefresh: () => void;
  children: ReactNode;
}

function DashboardShell({
  user,
  connected,
  lastUpdated,
  activeTab,
  attentionCount,
  message,
  onTabChange,
  onLogout,
  onRefresh,
  children,
}: DashboardShellProps) {
  return (
    <main className="app-shell">
      <aside className="rail" aria-label="Main navigation">
        <a className="brand-mark" href="#overview" aria-label="aquaSense home" onClick={() => onTabChange('overview')}>
          <Sprout size={22} strokeWidth={2.1} />
        </a>
        <div className="rail-rule" />
        <a className={`rail-link ${activeTab === 'overview' ? 'active' : ''}`} href="#overview" aria-label="Overview" title="Overview" onClick={() => onTabChange('overview')}>
          <Activity size={19} />
        </a>
        <a className="rail-link" href="#sensors" aria-label="Sensors" title="Sensors" onClick={() => onTabChange('overview')}>
          <Waves size={19} />
        </a>
        <a className="rail-link" href="#controls" aria-label="Controls" title="Controls" onClick={() => onTabChange('overview')}>
          <SlidersHorizontal size={19} />
        </a>
        <button
          className={`rail-link notification-link ${activeTab === 'notifications' ? 'active' : ''}`}
          type="button"
          aria-label={attentionCount ? `Notifications, ${attentionCount} active alerts` : 'Notifications'}
          title="Notifications"
          aria-current={activeTab === 'notifications' ? 'page' : undefined}
          onClick={() => onTabChange('notifications')}
        >
          <Bell size={19} />
          {attentionCount > 0 && <span className="notification-count">{attentionCount > 9 ? '9+' : attentionCount}</span>}
        </button>
        <div className="rail-bottom"><span className={`connection-dot ${connected ? 'online' : ''}`} /></div>
      </aside>

      <section className="workspace" id="overview">
        <header className="topbar">
          <div className="brand-lockup">
            <span className="brand-name">aquaSense</span>
            <span className="brand-divider" />
            <span className="site-name">GARDEN / NORTH BED</span>
          </div>
          <div className="topbar-right">
            <span className={`connection-state ${connected ? 'is-online' : ''}`}>
              {connected ? <Wifi size={15} /> : <WifiOff size={15} />}
              {connected ? 'Device live' : 'Device offline'}
            </span>
            <span className="topbar-date">
              {lastUpdated ? `Updated ${lastUpdated.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}` : 'Waiting for data'}
            </span>
            <div className="account-control">
              <span>{user}</span>
              <button onClick={onLogout} title="Sign out" aria-label="Sign out"><LogOut size={15} /></button>
            </div>
          </div>
        </header>

        <div className="content">
          {children}
          {message && <div className="toast" role="status">{message}</div>}
          <footer className="footer">
            <span>aquaSense <i>·</i> FIREBASE CONTROL</span>
            <span>
              <span className={`footer-dot ${connected ? 'online' : ''}`} />
              {connected ? 'DATA STREAM ACTIVE' : 'RECONNECTING'}
              <button onClick={onRefresh} aria-label="Retry connection"><RefreshCw size={12} /></button>
            </span>
          </footer>
        </div>
      </section>
    </main>
  );
}

export default DashboardShell;
