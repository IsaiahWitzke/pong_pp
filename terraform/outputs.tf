output "service_url" {
  description = "Public HTTPS URL of the signaling server. Use this in loader.js as wss://."
  value       = google_cloud_run_v2_service.signal.uri
}

output "image_repo" {
  description = "Artifact Registry repo URL for pushing images via 'docker push' or 'gcloud builds submit'."
  value       = "${var.region}-docker.pkg.dev/${var.project_id}/${google_artifact_registry_repository.pong.repository_id}"
}
