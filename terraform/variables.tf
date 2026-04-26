variable "project_id" {
  description = "GCP project ID to deploy into."
  type        = string
}

variable "region" {
  description = "GCP region (used for both Cloud Run and Artifact Registry)."
  type        = string
  default     = "us-east1"
}

variable "image_tag" {
  description = "Docker image tag in Artifact Registry (e.g. a git sha or 'latest')."
  type        = string
  default     = "latest"
}

variable "min_instances" {
  description = "Minimum running instances. 0 = scale to zero (free, but cold starts)."
  type        = number
  default     = 0
}

variable "concurrency" {
  description = "Max concurrent requests (incl. open WebSockets) per instance."
  type        = number
  default     = 200
}
