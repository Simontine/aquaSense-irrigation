import { useState, type FormEvent } from 'react';
import { ArrowRight, Sprout } from 'lucide-react';
import { logIn, signUp } from '../auth';

interface AuthScreenProps {
  onAuthenticated: (email: string) => void;
}

function AuthScreen({ onAuthenticated }: AuthScreenProps) {
  const [mode, setMode] = useState<'login' | 'signup'>('login');
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [confirmPassword, setConfirmPassword] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);

  const isSignup = mode === 'signup';

  const switchMode = () => {
    setMode(isSignup ? 'login' : 'signup');
    setError('');
    setPassword('');
    setConfirmPassword('');
  };

  const submit = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    setError('');
    if (isSignup && password !== confirmPassword) {
      setError('Those passwords do not match.');
      return;
    }

    setBusy(true);
    try {
      const authenticatedEmail = isSignup ? await signUp(email, password) : await logIn(email, password);
      onAuthenticated(authenticatedEmail);
    } catch (submitError) {
      setError(submitError instanceof Error ? submitError.message : 'Could not sign in. Please try again.');
    } finally {
      setBusy(false);
    }
  };

  return (
    <main className="auth-layout">
      <aside className="auth-aside">
        <a className="auth-brand" href="#" aria-label="aquaSense"><Sprout size={21} /><span>aquaSense</span></a>
        <div className="auth-story">
          <span className="auth-eyebrow">GROW WITH INTENTION</span>
          <h1>A little more care.<br /><em>Right on time.</em></h1>
          <p>Know what your garden needs, and let aquaSense take care of the rest.</p>
        </div>
        <span className="auth-aside-caption">aquaSense / GARDEN SYSTEMS</span>
      </aside>

      <section className="auth-main" aria-labelledby="auth-title">
        <div className="auth-form-wrap">
          <span className="auth-mobile-brand"><Sprout size={19} /> aquaSense</span>
          <div className="auth-form-heading">
            <span className="eyebrow"><span className="eyebrow-line" /> YOUR GARDEN, CONNECTED</span>
            <h2 id="auth-title">{isSignup ? 'Create your account' : 'Welcome back'}</h2>
            <p>{isSignup ? 'Start managing your garden with aquaSense.' : 'Sign in to check in on your garden.'}</p>
          </div>

          <form className="auth-form" onSubmit={submit}>
            <label htmlFor="auth-email">Email address</label>
            <input
              id="auth-email"
              type="email"
              autoComplete="email"
              value={email}
              onChange={(event) => setEmail(event.target.value)}
              placeholder="you@example.com"
              required
            />

            <label htmlFor="auth-password">Password</label>
            <input
              id="auth-password"
              type="password"
              autoComplete={isSignup ? 'new-password' : 'current-password'}
              value={password}
              onChange={(event) => setPassword(event.target.value)}
              placeholder={isSignup ? 'At least 8 characters' : 'Enter your password'}
              minLength={isSignup ? 8 : undefined}
              required
            />

            {isSignup && <>
              <label htmlFor="auth-confirm-password">Confirm password</label>
              <input
                id="auth-confirm-password"
                type="password"
                autoComplete="new-password"
                value={confirmPassword}
                onChange={(event) => setConfirmPassword(event.target.value)}
                placeholder="Enter your password again"
                minLength={8}
                required
              />
            </>}

            {error && <p className="auth-error" role="alert">{error}</p>}
            <button className="auth-submit" type="submit" disabled={busy}>
              <span>{busy ? 'Please wait…' : isSignup ? 'Create account' : 'Sign in'}</span>
              <ArrowRight size={17} />
            </button>
          </form>

          <p className="auth-switch">
            {isSignup ? 'Already have an account?' : 'New to aquaSense?'}{' '}
            <button type="button" onClick={switchMode}>{isSignup ? 'Sign in' : 'Create an account'}</button>
          </p>
          <p className="auth-local-note">Local demo access. Accounts are stored in this browser only and are not protected by a server.</p>
        </div>
      </section>
    </main>
  );
}

export default AuthScreen;