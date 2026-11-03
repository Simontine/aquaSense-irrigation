import { Activity, Check, CircleAlert, Droplets, X } from 'lucide-react';
import type { NotificationItem } from '../types/dashboard';

interface NotificationsPageProps {
  notifications: NotificationItem[];
  visibleNotifications: NotificationItem[];
  attentionCount: number;
  onDismiss: (id: string) => void;
  onBack: () => void;
}

function NotificationsPage({
  notifications,
  visibleNotifications,
  attentionCount,
  onDismiss,
  onBack,
}: NotificationsPageProps) {
  return (
    <section className="notifications-view" aria-labelledby="notifications-title">
      <div className="notifications-heading">
        <div>
          <span className="eyebrow"><span className="eyebrow-line" /> SYSTEM FEED</span>
          <h1 id="notifications-title">Notifications</h1>
          <p className="intro-copy">Live alerts from your irrigation system.</p>
        </div>
        <div className={`notification-summary ${attentionCount ? 'has-alerts' : ''}`}>
          <strong>{attentionCount}</strong>
          <span>{attentionCount === 1 ? 'needs attention' : 'need attention'}</span>
        </div>
      </div>

      <div className="notification-list" aria-live="polite">
        {visibleNotifications.length ? visibleNotifications.map((notification) => (
          <article className={`notification-item notification-${notification.level}`} key={notification.id}>
            <span className="notification-symbol">
              {notification.level === 'info' ? <Droplets size={17} /> : <CircleAlert size={17} />}
            </span>
            <div className="notification-copy">
              <div className="notification-title-row">
                <h2>{notification.title}</h2>
                <div className="notification-actions">
                  <span>{notification.level === 'critical' ? 'URGENT' : notification.level === 'warning' ? 'ATTENTION' : 'UPDATE'}</span>
                  <button
                    type="button"
                    className="dismiss-notification"
                    aria-label={`Dismiss ${notification.title}`}
                    title="Dismiss notification"
                    onClick={() => onDismiss(notification.id)}
                  >
                    <X size={15} />
                  </button>
                </div>
              </div>
              <p>{notification.detail}</p>
            </div>
          </article>
        )) : (
          <div className="notifications-empty">
            <span><Check size={18} /></span>
            <div>
              <strong>{notifications.length ? 'You’re all caught up' : 'All clear'}</strong>
              <p>{notifications.length
                ? 'Dismissed alerts will return if their conditions change.'
                : 'No active alerts. Your system is operating within its current limits.'}</p>
            </div>
          </div>
        )}
      </div>

      <button className="back-overview" type="button" onClick={onBack}>
        <Activity size={15} /> Back to overview
      </button>
    </section>
  );
}

export default NotificationsPage;
