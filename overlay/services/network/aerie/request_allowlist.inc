// Generated from docs/network-endpoints.md by: ./aerie netpolicy generate
// Do not edit by hand. Add a reviewed row to the doc and regenerate; a test fails when they differ.
// Included inside the anonymous namespace of request_policy.cc.

// A browser request with this traffic annotation may reach this host or a
// subdomain of it.
constexpr AllowedHost kAllowedHosts[] = {
    {"aerie_filter_list_download", "easylist.to"},
    {"update_client", "update.googleapis.com"},
    {"update_client", "dl.google.com"},
    {"update_client", "gvt1.com"},
};

// The user typed or chose the destination: allowed for any host.
constexpr DirectedAnnotation kUserDirectedAnnotations[] = {
    {"navigation_url_loader"},
    {"download_bubble_retry_download"},
    {"download_manager_resume"},
    {"download_web_contents_frame"},
    {"drag_download_file"},
    {"parallel_download_job"},
    {"save_file_manager"},
    {"render_view_context_menu"},
    {"dns_over_https"},
    {"proxy_config_direct"},
    {"proxy_config_settings"},
    {"proxy_config_system"},
    {"devtools_network_resource"},
    {"devtools_cdp_network_resource"},
    {"aerie_custom_filter_list_download"},
};

// A site being visited, or an extension the user installed, sets the
// destination (trusted by annotation): allowed for any host.
constexpr DirectedAnnotation kSiteDirectedAnnotations[] = {
    {"certificate_verifier_url_loader"},
    {"certificate_verifier_url_request"},
    {"certificate_verifier_2qwac_loader"},
    {"service_worker_script_load"},
    {"service_worker_update_checker"},
    {"service_worker_navigation_preload"},
    {"service_worker_race_network_request"},
    {"worker_script_load"},
    {"reporting"},
    {"dbsc_registration"},
    {"keepalive_fetch_retry"},
    {"background_fetch_context"},
    {"early_hints_preload"},
    {"speculation_rules_prefetch"},
    {"speculation_rules_prefetch_probe"},
    {"payment_manifest_downloader"},
    {"sigined_exchange_cert_fetcher"},
    {"webauthn_rp_id_check"},
    {"web_app_origin_association_download"},
    {"web_install_manifest_fetcher"},
    {"renderer_initiated_download"},
    {"websocket_basic_stream"},
    {"websocket_stream"},
    {"bidirectional_stream"},
    {"downloads_api_run_async"},
};
