terraform {
  required_version = ">= 1.5.0"

  required_providers {
    google = {
      source  = "hashicorp/google"
      version = "~> 5.0"
    }
  }
}

provider "google" {
  project = var.project_id
  region  = var.region
}

# ----- APIs we need turned on for the project -----

resource "google_project_service" "run" {
  service            = "run.googleapis.com"
  disable_on_destroy = false
}

resource "google_project_service" "artifact_registry" {
  service            = "artifactregistry.googleapis.com"
  disable_on_destroy = false
}

resource "google_project_service" "cloud_build" {
  service            = "cloudbuild.googleapis.com"
  disable_on_destroy = false
}

# ----- Container image storage -----

resource "google_artifact_registry_repository" "pong" {
  location      = var.region
  repository_id = "pong-pp"
  description   = "Container images for pong_pp signaling server"
  format        = "DOCKER"

  depends_on = [google_project_service.artifact_registry]
}

# ----- The signaling server itself -----

resource "google_cloud_run_v2_service" "signal" {
  name     = "pong-signal"
  location = var.region

  # Wait for required APIs to be enabled before trying to create the service.
  depends_on = [google_project_service.run]

  template {
    # Pin to a single instance so the in-memory rooms map stays
    # consistent across all client connections.
    scaling {
      max_instance_count = 1
      min_instance_count = var.min_instances
    }

    # Allow many concurrent WebSockets per instance.
    max_instance_request_concurrency = var.concurrency

    containers {
      image = "${var.region}-docker.pkg.dev/${var.project_id}/${google_artifact_registry_repository.pong.repository_id}/server:${var.image_tag}"

      ports {
        container_port = 8080
      }

      resources {
        limits = {
          cpu    = "1"
          memory = "512Mi"
        }
        # Request-based CPU billing: idle WebSockets cost ~nothing.
        cpu_idle = true
      }
    }
  }
}

# ----- Make the service publicly reachable from browsers -----

resource "google_cloud_run_v2_service_iam_member" "public" {
  project  = google_cloud_run_v2_service.signal.project
  location = google_cloud_run_v2_service.signal.location
  name     = google_cloud_run_v2_service.signal.name
  role     = "roles/run.invoker"
  member   = "allUsers"
}
