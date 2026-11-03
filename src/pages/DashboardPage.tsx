import { useEffect, useMemo, useState } from 'react';
import { firebaseApi, type FirebaseValue, type IrrigationMode, type IrrigationStatus } from '../api';
import DashboardShell from '../components/DashboardShell';
import NotificationsPage from './NotificationsPage';
import OverviewPage from './OverviewPage';
import type { AiInsight, DashboardTab, NotificationItem } from '../types/dashboard';

const defaultThreshold = 38;

function property(value: FirebaseValue | undefined, key: string): FirebaseValue | undefined {
  if (value !== null && typeof value === 'object' && !Array.isArray(value)) return value[key];
  return undefined;
}

function numericValue(value: FirebaseValue | undefined): number | null {
  return typeof value === 'number' && Number.isFinite(value) ? value : null;
}

function percentageValue(value: FirebaseValue | undefined): number | null {
  const number = numericValue(value);
  return number !== null && number >= 0 && number <= 100 ? number : null;
}

function hasFreshFirebaseStatus(status: IrrigationStatus | undefined): boolean {
  if (!status || !Number.isFinite(status.updatedAt)) return false;
  const age = Date.now() - status.updatedAt;
  return age >= 0 && age < 30_000;
}

function clamp(value: number, min: number, max: number): number {
  return Math.min(Math.max(value, min), max);
}

function buildAiInsight(input: {
  soil: number | null;
  tank: number | null;
  temperature: number | null;
  humidity: number | null;
  environmentAlerts: Array<{ summary: string; recommendation: string }>;
  threshold: number;
  pumpOn: boolean;
  tankLow: boolean;
  tankUnavailable: boolean;
}): AiInsight {
  const { soil, tank, temperature, humidity, environmentAlerts, threshold, pumpOn, tankLow, tankUnavailable } = input;

  if (tankUnavailable) {
    return {
      badge: 'Protective',
      headline: 'Tank-level reading unavailable',
      summary: 'The tank-level probe is missing or returning an invalid reading, so the firmware is keeping the pump locked off until it can verify the water reserve.',
      confidence: 95,
      recommendations: ['Check the tank probe and its empty/full calibration; the pump remains locked until a valid level is reported.'],
      recommendedThreshold: null,
    };
  }

  if (environmentAlerts.length > 0) {
    return {
      badge: 'Attention',
      headline: 'Environmental conditions need attention',
      summary: environmentAlerts.map((alert) => alert.summary).join(' '),
      confidence: 88,
      recommendations: environmentAlerts.map((alert) => alert.recommendation),
      recommendedThreshold: null,
    };
  }

  if (soil === null) {
    return {
      badge: 'Monitoring',
      headline: 'Collecting field data',
      summary: 'The soil sensor is not reporting a valid reading yet, so the AI is waiting for a fresh moisture sample before recommending action.',
      confidence: 58,
      recommendations: ['Check the soil probe wiring and placement.', 'Waiting for the next sensor refresh before dispatching a recommendation.'],
      recommendedThreshold: null,
    };
  }

  const recommendations: string[] = [];
  let confidence = 68;
  let headline = 'System is balanced';
  let summary = 'Current conditions are stable, and the irrigation schedule looks consistent with your target moisture level.';
  let recommendedThreshold: number | null = null;

  if (tankLow) {
    confidence += 12;
    headline = 'Water reserve is at risk';
    summary = 'The tank is below the safe operating threshold, so the system is protecting the pump from dry-running and the AI is prioritizing water conservation.';
    recommendations.push('Tank level is too low for safe watering. Keep the pump locked out until the reservoir is replenished.');
  } else if (soil < threshold - 8) {
    confidence += 18;
    headline = 'Moisture is trending dry';
    summary = 'The soil is below your target by a meaningful margin, so the irrigation cycle should be allowed to run soon.';
    recommendations.push('Watering is likely needed soon. If the trend continues, the next cycle should start before the lower threshold is crossed again.');
    recommendedThreshold = clamp(threshold + 2, 15, 75);
  } else if (soil > threshold + 10 && !pumpOn) {
    confidence += 14;
    headline = 'Soil is still comfortably wet';
    summary = 'Moisture is above the target range, so the AI suggests delaying watering and conserving water for the next dry period.';
    recommendations.push('Hold off on irrigation for the next cycle unless the weather turns hotter or drier.');
    recommendedThreshold = clamp(threshold - 2, 15, 75);
  } else {
    recommendations.push('Current moisture is stable and close to your target, so routine watering remains appropriate.');
  }

  if (tank !== null && tank < 30) {
    recommendations.push('Reservoir reserve is moderately low, so the system should avoid prolonged watering bursts.');
    confidence += 8;
  }

  if (temperature !== null && temperature > 29 && humidity !== null && humidity < 45) {
    confidence += 10;
    recommendations.push('Warm, dry air is increasing evapotranspiration, so the garden may need more frequent checks during the day.');
  }

  if (soil < threshold - 3 && pumpOn) {
    recommendations.push('The pump is actively watering the bed, which matches the current dry trend. Keep monitoring until the soil returns to range.');
  }

  if (recommendations.length === 0) {
    recommendations.push('Keep the current schedule and monitor the next sensor refresh for drift.');
  }

  return {
    badge: tankLow ? 'Protective' : soil < threshold ? 'Active' : 'Stable',
    headline,
    summary,
    confidence: clamp(confidence, 55, 97),
    recommendations,
    recommendedThreshold,
  };
}

interface DashboardPageProps {
  user: string;
  onLogout: () => void;
}

function DashboardPage({ user, onLogout }: DashboardPageProps) {
  const [status, setStatus] = useState<IrrigationStatus>();
  const [connected, setConnected] = useState(false);
  const [pendingMode, setPendingMode] = useState<IrrigationMode | null>(null);
  const [pendingPump, setPendingPump] = useState<boolean | null>(null);
  const [threshold, setThreshold] = useState(defaultThreshold);
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState('');
  const [lastUpdated, setLastUpdated] = useState<Date | null>(null);
  const [refreshTick, setRefreshTick] = useState(0);
  const [sensorData, setSensorData] = useState<FirebaseValue>();
  const [firebaseError, setFirebaseError] = useState('');
  const [activeTab, setActiveTab] = useState<DashboardTab>('overview');
  const [dismissedNotifications, setDismissedNotifications] = useState<Set<string>>(() => new Set());

  useEffect(() => {
    let alive = true;
    const refresh = async () => {
      try {
        const snapshot = await firebaseApi.snapshot();
        if (!alive) return;
        const next = snapshot.status ?? undefined;
        setStatus(next);
        setSensorData(snapshot.sensorData ?? undefined);
        if (next) setThreshold(next.moistureThreshold);
        const fresh = hasFreshFirebaseStatus(next);
        setConnected(fresh);
        setLastUpdated(fresh && next ? new Date(next.updatedAt) : null);
        setFirebaseError([
          snapshot.statusError,
          next ? (fresh ? null : 'ESP32 status is stale') : snapshot.statusError || 'Waiting for ESP32 status',
          snapshot.sensorDataError ? `sensorData: ${snapshot.sensorDataError}` : null,
        ].filter(Boolean).join(' · '));
      } catch (error) {
        if (alive) {
          setConnected(false);
          setFirebaseError(error instanceof Error ? error.message : 'Could not load Firebase data');
        }
      }
    };
    void refresh();
    const timer = window.setInterval(() => void refresh(), 5000);
    return () => {
      alive = false;
      window.clearInterval(timer);
    };
  }, [refreshTick, user]);

  useEffect(() => {
    if (!pendingMode) return;
    if (status?.mode === pendingMode) {
      setPendingMode(null);
      setMessage(`ESP32 confirmed ${pendingMode} mode`);
      return;
    }

    const timeout = window.setTimeout(() => {
      setPendingMode(null);
      setMessage(`ESP32 has not confirmed ${pendingMode} mode. Check the device connection and firmware.`);
    }, 15_000);
    return () => window.clearTimeout(timeout);
  }, [pendingMode, status?.mode]);

  useEffect(() => {
    if (pendingPump === null) return;
    if (status?.pumpOn === pendingPump) {
      setPendingPump(null);
      setMessage(`Firebase confirmed pump ${pendingPump ? 'started' : 'stopped'}`);
      return;
    }

    const timeout = window.setTimeout(() => {
      setPendingPump(null);
      setMessage(`Firebase has not confirmed the pump ${pendingPump ? 'start' : 'stop'} command. Check the device connection and firmware.`);
    }, 15_000);
    return () => window.clearTimeout(timeout);
  }, [pendingPump, status?.pumpOn]);

  const runCommand = async (command: () => Promise<unknown>, success: string): Promise<boolean> => {
    setBusy(true);
    setMessage('');
    try {
      await command();
      setMessage(`${success} in Firebase`);
      return true;
    } catch (error) {
      setMessage(error instanceof Error ? error.message : 'Command failed');
      return false;
    } finally {
      setBusy(false);
    }
  };

  const changeMode = (mode: IrrigationMode) => {
    setPendingMode(mode);
    void runCommand(() => firebaseApi.control(mode), `${mode} mode command sent; waiting for ESP32 confirmation`)
      .then((sent) => {
        if (!sent) setPendingMode(null);
      });
  };

  const saveThreshold = () => {
    void runCommand(() => firebaseApi.configure(threshold), 'Moisture threshold saved');
  };

  const pumpToggle = () => {
    if (!status) return;
    const requestedPump = !status.pumpOn;
    setPendingPump(requestedPump);
    void runCommand(
      () => firebaseApi.control('manual', requestedPump),
      `Pump ${requestedPump ? 'start' : 'stop'} command sent`,
    ).then((sent) => {
      if (!sent) setPendingPump(null);
    });
  };

  const firebaseSensorData = sensorData;
  const firebaseStatusLabel = connected ? 'DEVICE ONLINE' : status ? 'STATUS STALE' : 'NO STATUS RECORD';
  const firebaseSoil = numericValue(property(firebaseSensorData, 'soilMoisture'));
  const firebaseTank = percentageValue(property(firebaseSensorData, 'tankLevel'));
  const firebaseTemperature = numericValue(property(firebaseSensorData, 'temp'));
  const firebaseHumidity = percentageValue(property(firebaseSensorData, 'humidity'));
  const controllerSoil = status?.soilValid ? status.soilPercent : null;
  const controllerTank = status?.tankValid ? status.tankPercent : null;
  const tankRaw = status?.tankRaw ?? numericValue(property(firebaseSensorData, 'tankRaw'));
  const soil = firebaseSoil ?? controllerSoil;
  const soilPercent = firebaseSoil === null ? controllerSoil : firebaseSoil >= 0 && firebaseSoil <= 100 ? firebaseSoil : null;
  const tank = status?.tankValid ? firebaseTank ?? controllerTank : null;
  const temperature = firebaseTemperature ?? (status?.climateValid ? status.temperatureC : null);
  const humidity = firebaseHumidity ?? (status?.climateValid ? status.humidityPercent : null);
  const tankLow = tank !== null && tank < 18;
  const tankUnavailable = connected && tank === null;
  const tankWarning = tank !== null && tank < 20;
  const temperatureOutOfRange = temperature !== null && (temperature < 20 || temperature > 35);
  const temperatureAlertDetail = temperature !== null && temperature < 20
    ? `Temperature is below 20°C (${Math.round(temperature)}°C).`
    : `Temperature is above 35°C (${Math.round(temperature ?? 0)}°C).`;
  const humidityOutOfRange = humidity !== null && (humidity < 20 || humidity > 70);
  const soilVeryWet = soilPercent !== null && soilPercent > threshold + 10 && !status?.pumpOn;
  const environmentAlerts = [
    ...(temperatureOutOfRange ? [{
      summary: temperatureAlertDetail,
      recommendation: 'Check the garden temperature and protect plants from excessive heat or cold.',
    }] : []),
    ...(humidityOutOfRange ? [{
      summary: `Humidity is ${Math.round(humidity)}%, outside the 20–70% range.`,
      recommendation: 'Check airflow and plant conditions; very dry or humid air can stress the garden.',
    }] : []),
    ...(tankWarning ? [{
      summary: `Tank level is ${Math.round(tank)}%, below the 20% reserve alert.`,
      recommendation: 'Refill the reservoir soon. The pump remains locked out if the level falls below 18%.',
    }] : []),
  ];
  const notifications: NotificationItem[] = [
    ...(!connected ? [{ id: 'controller', level: 'warning' as const, title: 'Irrigation device is offline', detail: 'Check the device Wi-Fi connection and Firebase sync to restore live control.' }] : []),
    ...(tankUnavailable ? [{ id: 'tank-sensor', level: 'warning' as const, title: 'Tank-level reading unavailable', detail: 'The firmware is keeping the pump locked off until the probe reports a valid tank level.' }] : []),
    ...(tankWarning ? [{ id: 'tank', level: tankLow ? 'critical' as const : 'warning' as const, title: tankLow ? 'Water reserve is too low' : 'Water reserve is low', detail: tankLow ? `Tank level is ${Math.round(tank ?? 0)}%. Refill before running the pump.` : `Tank level is ${Math.round(tank ?? 0)}%, below the 20% reserve alert. Refill soon.` }] : []),
    ...(temperatureOutOfRange ? [{ id: 'temperature', level: 'warning' as const, title: temperature !== null && temperature < 20 ? 'Temperature is below 20°C' : 'Temperature is above 35°C', detail: temperatureAlertDetail }] : []),
    ...(humidityOutOfRange ? [{ id: 'humidity', level: 'warning' as const, title: 'Humidity is outside range', detail: `Humidity is ${Math.round(humidity ?? 0)}%. Expected range: 20–70%.` }] : []),
    ...(soilPercent !== null && soilPercent < threshold ? [{ id: 'soil', level: 'warning' as const, title: 'Soil moisture is below target', detail: `Moisture is ${Math.round(soilPercent)}%, below your ${threshold}% target.` }] : []),
    ...(soilVeryWet ? [{ id: 'soil-wet', level: 'warning' as const, title: 'Soil moisture is well above target', detail: `Moisture is ${Math.round(soilPercent)}%, more than 10 points above your ${threshold}% target. Consider delaying the next watering cycle.` }] : []),
    ...(connected && soilPercent === null ? [{ id: 'soil-sensor', level: 'warning' as const, title: 'Soil sensor has no valid reading', detail: 'Check the probe connection; automatic irrigation needs a valid moisture value.' }] : []),
    ...(firebaseError ? [{ id: 'firebase', level: 'warning' as const, title: 'Firebase data is unavailable', detail: firebaseError }] : []),
    ...(status?.pumpOn ? [{ id: 'pump', level: 'info' as const, title: 'Watering is in progress', detail: 'The irrigation pump is currently running.' }] : []),
  ];
  const notificationIds = notifications.map((notification) => notification.id).join('|');

  useEffect(() => {
    const activeIds = new Set(notificationIds.split('|').filter(Boolean));
    setDismissedNotifications((dismissed) => {
      const next = new Set([...dismissed].filter((id) => activeIds.has(id)));
      return next.size === dismissed.size ? dismissed : next;
    });
  }, [notificationIds]);

  const visibleNotifications = notifications.filter((notification) => !dismissedNotifications.has(notification.id));
  const attentionCount = visibleNotifications.filter((notification) => notification.level !== 'info').length;
  const aiInsight = useMemo(
    () => buildAiInsight({
      soil: soilPercent,
      tank,
      temperature,
      humidity,
      environmentAlerts,
      threshold,
      pumpOn: Boolean(status?.pumpOn),
      tankLow,
      tankUnavailable,
    }),
    [environmentAlerts, humidity, soilPercent, status?.pumpOn, tank, tankLow, tankUnavailable, temperature, threshold],
  );

  const applyAiThreshold = () => {
    const recommendedThreshold = aiInsight.recommendedThreshold;
    if (recommendedThreshold === null) return;
    setThreshold(recommendedThreshold);
    void runCommand(() => firebaseApi.configure(recommendedThreshold), 'AI threshold adjusted');
  };

  const handleLogout = () => {
    onLogout();
  };

  return (
    <DashboardShell
      user={user}
      connected={connected}
      lastUpdated={lastUpdated}
      activeTab={activeTab}
      attentionCount={attentionCount}
      message={message}
      onTabChange={setActiveTab}
      onLogout={handleLogout}
      onRefresh={() => setRefreshTick((tick) => tick + 1)}
    >
      {activeTab === 'notifications' ? (
        <NotificationsPage
          notifications={notifications}
          visibleNotifications={visibleNotifications}
          attentionCount={attentionCount}
          onDismiss={(id) => setDismissedNotifications((dismissed) => new Set(dismissed).add(id))}
          onBack={() => setActiveTab('overview')}
        />
      ) : (
        <OverviewPage
          connected={connected}
          status={status}
          soil={soil}
          soilPercent={soilPercent}
          tank={tank}
          tankRaw={tankRaw}
          temperature={temperature}
          humidity={humidity}
          tankLow={tankLow}
          tankUnavailable={tankUnavailable}
          threshold={threshold}
          pendingMode={pendingMode}
          pendingPump={pendingPump}
          busy={busy}
          aiInsight={aiInsight}
          firebaseStatusLabel={firebaseStatusLabel}
          lastUpdated={lastUpdated}
          sensorData={sensorData}
          onRefresh={() => setRefreshTick((tick) => tick + 1)}
          onChangeMode={changeMode}
          onPumpToggle={pumpToggle}
          onThresholdChange={setThreshold}
          onSaveThreshold={saveThreshold}
          onApplyAiThreshold={applyAiThreshold}
        />
      )}
    </DashboardShell>
  );
}

export default DashboardPage;
