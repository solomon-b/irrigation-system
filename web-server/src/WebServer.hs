{-# LANGUAGE QuantifiedConstraints #-}
module WebServer where

--------------------------------------------------------------------------------

import App qualified
import App.Auth qualified as Auth
import App.Observability (WithSpan)
import Data.Text (Text)
import OpenTelemetry.Trace (Tracer)
import Servant qualified
import Servant ((:>))
import qualified Data.Aeson as Aeson
import GHC.Generics (Generic)
import Data.Text.Display.Core (Display)
import Data.Text.Display.Generic (RecordInstance (..))
import App.Monad (AppM (..))
import qualified App.Config

--------------------------------------------------------------------------------

runApp :: () -> IO ()
runApp = App.runApp @API server

type API = WithSpan "GET SCHEDULE" (Servant.Header "Cookie" Text :> Servant.Get '[Servant.JSON] Schedule)

server :: App.Config.Environment -> Servant.ServerT API (AppM ())
server _ = handler

handler ::
  Tracer ->
  Maybe Text ->
  AppM () Schedule
handler _tracer cookie = do
    _loginState <- Auth.userLoginState cookie
    pure $ Schedule True False True

data Schedule = Schedule
  { zone1 :: Bool,
    zone2 :: Bool,
    zone3 :: Bool
  }
  deriving stock (Show, Generic)
  deriving anyclass (Aeson.FromJSON, Aeson.ToJSON)
  deriving (Display) via (RecordInstance Schedule)
