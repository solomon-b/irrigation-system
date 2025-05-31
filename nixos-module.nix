{ self }:
{ config, lib, pkgs, ... }:

let
  cfg = config.services.irrigation-web-server;
  system = pkgs.stdenv.hostPlatform.system;
  pkg = self.packages.${system}.irrigation-web-server;
in
{
  options.services.irrigation-web-server = {
    enable = lib.mkEnableOption "Enable the Irrigation web server";

    environment = lib.mkOption {
      type = lib.types.str;
      default = "Development";
      description = "Application environment (e.g. Development, Production)";
    };

    hostname = lib.mkOption {
      type = lib.types.str;
      default = "irrigation-web-server.local";
      description = "Application hostname";
    };

    postgres = {
      db = lib.mkOption { type = lib.types.str; default = "dev_db"; };
      host = lib.mkOption { type = lib.types.str; default = "localhost"; };
      password = lib.mkOption {
        type = lib.types.nullOr lib.types.str;
        default = null;
        description = "Postgres password as a string (insecure, prefer passwordFile)";
      };
      passwordFile = lib.mkOption {
        type = lib.types.nullOr lib.types.path;
        default = null;
        description = "Path to a file containing the Postgres password (not world-readable)";
      };
      port = lib.mkOption { type = lib.types.port; default = 5432; };
      user = lib.mkOption { type = lib.types.str; default = "postgres"; };
    };

    warp = {
      port = lib.mkOption { type = lib.types.port; default = 2000; };
      serverName = lib.mkOption { type = lib.types.str; default = "localhost"; };
      timeout = lib.mkOption { type = lib.types.str; default = "100"; };
    };

    observability = {
      verbosity = lib.mkOption { type = lib.types.str; default = "Debug"; };
      exporter = lib.mkOption { type = lib.types.str; default = "Otel"; };
    };

    otel = {
      sampler = lib.mkOption { type = lib.types.str; default = "always_on"; };
      serviceName = lib.mkOption { type = lib.types.str; default = "irrigation-web-server"; };
      endpoint = lib.mkOption { type = lib.types.str; default = "http://localhost:4318"; };
      protocol = lib.mkOption { type = lib.types.str; default = "http/protobuf"; };
    };
  };

  config = lib.mkIf cfg.enable {
    assertions = lib.mkIf cfg.enable [
      {
        assertion = cfg.postgres.password != null || cfg.postgres.passwordFile != null;
        message = "You must set either services.irrigation-web-server.postgres.password or passwordFile.";
      }
    ];

    systemd.services.irrigation-web-server = {
      wantedBy = [ "multi-user.target" ];
      after = [ "network.target" ];
      description = "Haskell Web Server";
      serviceConfig = {
        ExecStart = "${pkg}/bin/irrigation-web-server";
        Restart = "on-failure";
        DynamicUser = true;

        LoadCredential = lib.mkIf (cfg.postgres.passwordFile != null) [
          "pgpass:${cfg.postgres.passwordFile}"
        ];

        Environment = [
          "APP_ENVIRONMENT=${cfg.environment}"
          "APP_HOSTNAME=${cfg.hostname}"

          "APP_POSTGRES_DB=${cfg.postgres.db}"
          "APP_POSTGRES_HOST=${cfg.postgres.host}"
          "APP_POSTGRES_PORT=${toString cfg.postgres.port}"
          "APP_POSTGRES_USER=${cfg.postgres.user}"

          "APP_WARP_PORT=${toString cfg.warp.port}"
          "APP_WARP_SERVERNAME=${cfg.warp.serverName}"
          "APP_WARP_TIMEOUT=${cfg.warp.timeout}"

          "APP_OBSERVABILITY_VERBOSITY=${cfg.observability.verbosity}"
          "APP_OBSERVABILITY_EXPORTER=${cfg.observability.exporter}"

          "OTEL_TRACES_SAMPLER=${cfg.otel.sampler}"
          "OTEL_SERVICE_NAME=${cfg.otel.serviceName}"
          "OTEL_EXPORTER_OTLP_ENDPOINT=${cfg.otel.endpoint}"
          "OTEL_EXPORTER_OTLP_PROTOCOL=${cfg.otel.protocol}"
        ] ++ (if cfg.postgres.passwordFile != null then
          [ "APP_POSTGRES_PASSWORD=\${CREDENTIALS_DIRECTORY}/pgpass" ]
        else
          [ "APP_POSTGRES_PASSWORD=${cfg.postgres.password}" ]);
      };
    };
  };
}
