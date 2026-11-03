import { useState } from 'react';
import AuthPage from './pages/AuthPage';
import DashboardPage from './pages/DashboardPage';
import { currentSession, logOut } from './auth';

function App() {
  const [user, setUser] = useState<string | null>(() => currentSession());

  const handleLogout = () => {
    logOut();
    setUser(null);
  };

  if (!user) return <AuthPage onAuthenticated={setUser} />;

  return <DashboardPage user={user} onLogout={handleLogout} />;
}

export default App;
