import type { FirebaseValue, IrrigationMode, IrrigationStatus } from '../api';

export type DashboardTab = 'overview' | 'notifications';

export interface NotificationItem {
  id: string;
  level: 'warning' | 'critical' | 'info';
  title: string;
  detail: string;
}

export interface AiInsight {
  badge: string;
  headline: string;
  summary: string;
  confidence: number;
  recommendations: string[];
  recommendedThreshold: number | null;
}

export interface OverviewPageProps {
  connected: boolean;
  status: IrrigationStatus | undefined;
  soil: number | null;
  soilPercent: number | null;
  tank: number | null;
  tankRaw: number | null;
  temperature: number | null;
  humidity: number | null;
  tankLow: boolean;
  tankUnavailable: boolean;
  threshold: number;
  pendingMode: IrrigationMode | null;
  pendingPump: boolean | null;
  busy: boolean;
  aiInsight: AiInsight;
  firebaseStatusLabel: string;
  lastUpdated: Date | null;
  sensorData: FirebaseValue | undefined;
  onRefresh: () => void;
  onChangeMode: (mode: IrrigationMode) => void;
  onPumpToggle: () => void;
  onThresholdChange: (threshold: number) => void;
  onSaveThreshold: () => void;
  onApplyAiThreshold: () => void;
}
